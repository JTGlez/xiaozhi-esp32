/* Host harness for the ElenaAvatarDisplay tests.
 *
 * It instantiates the REAL ElenaAvatarDisplay (which inherits the real
 * SpiLcdDisplay/LcdDisplay/LvglDisplay production classes) and drives it
 * through semantic events, then asserts observable widget state: hidden
 * flags, label text, image src, decoder identity, esp_timer registrations.
 *
 * Each selector asserts mutations on actual widgets through a derived class
 * that exposes the production protected widget pointers; nothing here reads
 * production source strings.
 *
 * Checkpoint status: 'hidden_conversation' is implemented. Every other
 * selector exits nonzero with a MISSING CASE diagnostic so the Python suite
 * can never report a fake PASS.
 */
#include "elena_avatar_display.h"

#include "board.h"
#include "emoji_collection.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "lvgl_image.h"
#include "lvgl_theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define CHECK(cond, msg)                                                                          \
    do {                                                                                          \
        if (!(cond)) {                                                                            \
            std::fprintf(stderr, "CHECK FAILED: %s\n  at %s:%d: %s\n", #cond, __FILE__, __LINE__, \
                         (msg));                                                                  \
            std::exit(1);                                                                         \
        }                                                                                         \
    } while (0)

namespace {

/* ------------------------------------------------------------- utilities */

std::map<std::string, std::vector<uint8_t>>& asset_cache() {
    static std::map<std::string, std::vector<uint8_t>> cache;
    return cache;
}

std::vector<uint8_t>* asset_bytes(const std::string& dir, const std::string& key) {
    auto& cache = asset_cache();
    auto it = cache.find(key);
    if (it != cache.end()) {
        return &it->second;
    }
    std::string name = key;
    std::string mode;
    auto hash = key.find('#');
    if (hash != std::string::npos) {
        name = key.substr(0, hash);
        mode = key.substr(hash + 1);
    }
    std::ifstream file(dir + "/" + name + ".gif", std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    if (file.fail() || data.empty()) {
        std::fprintf(stderr, "missing asset: %s/%s.gif\n", dir.c_str(), name.c_str());
        std::exit(2);
    }
    if (mode == "mutated") {
        data[data.size() - 5] ^= 1;
    } else if (mode == "zero") {
        // Valid GIF magic but zero canvas: passes the resource pre-check,
        // fails inside the real gifdec.c open.
        data[6] = 0;
        data[7] = 0;
        data[8] = 0;
        data[9] = 0;
    } else if (mode == "truncated") {
        data.resize(15);
    } else if (mode == "notgif") {
        data[0] = 'X';  // breaks the IsGif() sniff
    }
    auto inserted = cache.emplace(key, std::move(data));
    return &inserted.first->second;
}

std::vector<std::string> all_emotions() {
    return {"email", "happy", "listening", "look_left", "look_right", "neutral",
            "pet",   "sad",   "shake",     "sleeping",  "speaking",   "thinking"};
}

std::shared_ptr<EmojiCollection> make_collection(const std::string& dir,
                                                 const std::vector<std::string>& keys) {
    auto collection = std::make_shared<EmojiCollection>();
    for (const auto& key : keys) {
        std::string name = key.substr(0, key.find('#'));
        auto* data = asset_bytes(dir, key);
        collection->AddEmoji(name, new LvglRawImage(data->data(), data->size()));
    }
    return collection;
}

/* Keeps every created collection (and its image buffers) alive for the whole
 * process: live GIF decoders borrow image data from these collections. */
std::vector<std::shared_ptr<EmojiCollection>>& collection_registry() {
    static std::vector<std::shared_ptr<EmojiCollection>> registry;
    return registry;
}

LvglTheme* theme_named(const char* name) { return LvglThemeManager::GetInstance().GetTheme(name); }

#define CHECK_LOCAL(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "harness setup failure: %s\n", #cond); \
            std::exit(2);                                               \
        }                                                               \
    } while (0)

EmojiCollection* attach_theme(const std::shared_ptr<EmojiCollection>& collection,
                              const char* theme_name) {
    collection_registry().push_back(collection);
    LvglTheme* theme = theme_named(theme_name);
    CHECK_LOCAL(theme != nullptr);
    theme->set_emoji_collection(collection);
    return collection.get();
}

/* Derived view: exposes the production protected widget pointers for
 * observation. Adds no behavior of its own. */
class ElenaHarness : public ElenaAvatarDisplay {
public:
    using ElenaAvatarDisplay::ElenaAvatarDisplay;

    lv_obj_t* statusLabel() { return status_label_; }
    lv_obj_t* notificationLabel() { return notification_label_; }
    lv_obj_t* emojiImage() { return emoji_image_; }
    lv_obj_t* emojiLabel() { return emoji_label_; }
    lv_obj_t* emojiBox() { return emoji_box_; }
    lv_obj_t* previewImage() { return preview_image_; }
    lv_obj_t* topBar() { return top_bar_; }
    lv_obj_t* container() { return container_; }
    lv_obj_t* bottomBar() { return bottom_bar_; }
    lv_obj_t* chatLabel() { return chat_message_label_; }
    lv_obj_t* lowBatteryPopup() { return low_battery_popup_; }
    lv_obj_t* batteryLabel() { return battery_label_; }
    lv_obj_t* networkLabel() { return network_label_; }
    lv_obj_t* muteLabel() { return mute_label_; }
    LvglGif* gif() { return gif_controller_.get(); }
    const lv_img_dsc_t* emoji_src() {
        return static_cast<const lv_img_dsc_t*>(host_lvgl_obj_img_src(emoji_image_));
    }
};

std::unique_ptr<ElenaHarness> make_display() {
    static int io_stub = 0x10;
    static int panel_stub = 0x20;
    return std::make_unique<ElenaHarness>(reinterpret_cast<esp_lcd_panel_io_handle_t>(&io_stub),
                                          reinterpret_cast<esp_lcd_panel_handle_t>(&panel_stub),
                                          320, 240, 0, 0, false, false, false);
}

ConversationVisualEvent event_of(ConversationVisualEventKind kind, uint64_t now,
                                 uint32_t generation = 0,
                                 ConversationActivity activity = ConversationActivity::Idle,
                                 bool playback_idle = true) {
    ConversationVisualEvent event{};
    event.kind = kind;
    event.generation = generation;
    event.now_ms = now;
    event.activity = activity;
    event.playback_idle = playback_idle;
    return event;
}

void handle(ElenaHarness* display, const ConversationVisualEvent& event) {
    display->OnConversationVisualEvent(event);
}

bool hidden(lv_obj_t* obj) { return obj != nullptr && host_lvgl_obj_hidden(obj); }
const char* text_of(lv_obj_t* obj) { return obj != nullptr ? host_lvgl_obj_text(obj) : ""; }
bool tree_has(const char* needle) { return host_lvgl_tree_has_text(lv_screen_active(), needle); }

std::unique_ptr<LvglImage> make_preview_image(const std::string& dir) {
    auto* data = asset_bytes(dir, "neutral");
    return std::make_unique<LvglRawImage>(data->data(), data->size());
}

/* ------------------------------------------------------------- selectors */

/* Conversation and spoken-notification wording must never become visible or
 * pending text on any Elena widget, for both SetHideSubtitle values, while a
 * preview is open, and after a theme change. Mutation target: forwarding
 * SetChatMessage/SetStatus to the base or honoring SetHideSubtitle(false). */
int case_hidden_conversation(const std::string& dir) {
    host_set_clock_ms(1000);
    auto display = make_display();
    auto collection = make_collection(dir, all_emotions());
    attach_theme(collection, "light");
    attach_theme(collection, "dark");
    display->SetupUI();

    CHECK(display->bottomBar() == nullptr, "Elena must never create a bottom bar");
    CHECK(display->chatLabel() == nullptr, "Elena must never create a chat label");
    CHECK(hidden(display->statusLabel()), "status label must start hidden");
    CHECK(std::strcmp(text_of(display->statusLabel()), "") == 0,
          "status label must start with no text");
    CHECK(hidden(display->notificationLabel()), "notification label must start hidden");
    CHECK(hidden(display->emojiLabel()) && std::strcmp(text_of(display->emojiLabel()), "") == 0,
          "font-emoji label must never carry text");

    auto assert_hidden = [&](const char* needle) {
        CHECK(!tree_has(needle), "conversation text reached the widget tree");
        CHECK(hidden(display->statusLabel()), "status label became visible");
        CHECK(std::strcmp(text_of(display->statusLabel()), "") == 0,
              "status label retained conversation text");
        CHECK(hidden(display->notificationLabel()), "notification label became visible");
        CHECK(std::strcmp(text_of(display->notificationLabel()), "") == 0,
              "notification label retained conversation text");
    };

    // Normal conversation state (a live listening turn).
    handle(display.get(), event_of(ConversationVisualEventKind::TurnStarted, 2000, 1,
                                   ConversationActivity::Listening));

    display->SetHideSubtitle(true);
    display->SetChatMessage("user", "CAP-USER-11");
    assert_hidden("CAP-USER-11");

    display->SetHideSubtitle(false);
    display->SetChatMessage("user", "CAP-USER-12");
    assert_hidden("CAP-USER-12");
    display->SetChatMessage("assistant", "CAP-ASST-22");
    assert_hidden("CAP-ASST-22");

    // Spoken notification/system conversation text (semantic notification
    // cycle active, playback not idle).
    handle(display.get(), event_of(ConversationVisualEventKind::NotificationStarted, 3000, 1,
                                   ConversationActivity::Listening, false));
    handle(display.get(), event_of(ConversationVisualEventKind::PlaybackState, 3100, 1,
                                   ConversationActivity::Listening, false));
    display->SetChatMessage("system", "CAP-SYS-33");
    assert_hidden("CAP-SYS-33");

    // While a preview is open.
    display->SetPreviewImage(make_preview_image(dir));
    CHECK(!hidden(display->previewImage()), "preview image must be visible while open");
    display->SetChatMessage("user", "CAP-PREV-44");
    assert_hidden("CAP-PREV-44");
    display->SetPreviewImage(nullptr);
    CHECK(hidden(display->previewImage()), "preview image must be hidden after close");

    // After a theme change.
    display->SetTheme(theme_named("dark"));
    display->SetChatMessage("user", "CAP-THEME-55");
    assert_hidden("CAP-THEME-55");

    // Conversation-style SetStatus (no semantic notice owner) stays hidden.
    display->SetStatus("CAP-STATUS-66");
    assert_hidden("CAP-STATUS-66");

    // The emoji label font fallback must never surface any of this.
    CHECK(hidden(display->emojiLabel()), "font-emoji label became visible");
    CHECK(std::strcmp(text_of(display->emojiLabel()), "") == 0, "font-emoji label carried text");

    return 0;
}

int case_behavior(const std::string& selector, const std::string& dir) {
    host_set_clock_ms(1000);
    auto display = make_display();
    auto collection = make_collection(dir, all_emotions());
    attach_theme(collection, "light");
    attach_theme(collection, "dark");
    if (selector == "same_expression_pre_ui")
        display->SetEmotion("happy");
    display->SetupUI();
    auto emotion = [&](const char* name) {
        auto e = event_of(ConversationVisualEventKind::Emotion, host_clock_ms());
        e.emotion = name;
        display->OnConversationVisualEvent(e);
    };
    auto tick = [&]() {
        handle(display.get(), event_of(ConversationVisualEventKind::Tick, host_clock_ms()));
    };
    if (selector == "layout_idempotent") {
        CHECK(lv_obj_is_valid(display->container()), "canvas container was never created");
        auto count = host_lvgl_live_obj_count();
        display->SetupUI();
        CHECK(count == host_lvgl_live_obj_count(), "SetupUI rebuilt widgets");
        CHECK(display->bottomBar() == nullptr && display->chatLabel() == nullptr,
              "bottom text created");
        CHECK(host_lvgl_obj_w(display->emojiBox()) == 320 &&
                  host_lvgl_obj_h(display->emojiBox()) == 240,
              "canvas cropped");
        CHECK(host_lvgl_obj_scale(display->emojiImage()) == 256, "avatar scaled");
        CHECK(host_lvgl_obj_bg_opa(display->topBar()) == LV_OPA_TRANSP, "opaque indicators");
    } else if (selector == "same_expression_pre_ui" || selector == "same_expression_events") {
        emotion("happy");
        auto gif = display->gif();
        auto allocations = host_lvgl_big_alloc_count();
        CHECK(gif != nullptr && gif->IsLoaded(), "first expression missing");
        for (int i = 0; i < 20; ++i) {
            emotion("happy");
            tick();
        }
        CHECK(display->gif() == gif, "same expression reopened decoder");
        CHECK(host_lvgl_big_alloc_count() == allocations, "same expression allocated frame");
        emotion("sad");
        CHECK(host_lvgl_big_alloc_count() > allocations, "changed expression never applied");
    } else if (selector == "operational_notice" || selector == "operational_maintenance" ||
               selector == "spoken_not_operational") {
        auto kind = selector == "operational_maintenance"
                        ? ConversationVisualEventKind::Activity
                        : ConversationVisualEventKind::NoticeStarted;
        auto notice = event_of(kind, 1000, 0, ConversationActivity::Maintenance);
        handle(display.get(), notice);
        display->SetChatMessage("system", "CONFIG-OPERATION");
        CHECK(tree_has("CONFIG-OPERATION"), "operational instruction hidden");
        emotion("happy");
        display->ClearChatMessages();
        display->SetChatMessage("assistant", "SPOKEN-SECRET");
        CHECK(tree_has("CONFIG-OPERATION"), "emotion/conversation replaced notice");
        CHECK(!tree_has("SPOKEN-SECRET"), "spoken text exposed under notice");
        if (kind == ConversationVisualEventKind::Activity)
            handle(display.get(), event_of(kind, 1100));
        else
            handle(display.get(), event_of(ConversationVisualEventKind::NoticeCleared, 1100));
        CHECK(!tree_has("CONFIG-OPERATION"), "resolved notice remains visible");
    } else if (selector.find("notification_expiry_") == 0 ||
               selector == "notice_survives_notification") {
        if (selector == "notice_survives_notification") {
            handle(display.get(), event_of(ConversationVisualEventKind::NoticeStarted, 1000));
            display->SetStatus("ALERT-OWNER");
        }
        if (selector == "notification_expiry_string_overload")
            display->ShowNotification(std::string("VOLUME"), 20);
        else
            display->ShowNotification("VOLUME", 20);
        host_esp_timer_fire("elena_notification_expiry");
        display->UpdateStatusBar(true);
        if (selector == "notice_survives_notification")
            CHECK(tree_has("ALERT-OWNER"), "notification hid persistent notice");
        else
            CHECK(hidden(display->statusLabel()), "notification/clock restored normal status");
    } else if (selector.find("preview_") == 0) {
        if (selector == "preview_notice_priority") {
            handle(display.get(), event_of(ConversationVisualEventKind::NoticeStarted, 1000));
            display->SetStatus("ALERT-OWNER");
            display->SetPreviewImage(make_preview_image(dir));
            CHECK(hidden(display->previewImage()), "preview displaced notice");
            CHECK(tree_has("ALERT-OWNER"), "preview hid notice");
        } else {
            display->SetPreviewImage(make_preview_image(dir));
            CHECK(!hidden(display->previewImage()), "preview not shown");
            auto allocations = host_lvgl_big_alloc_count();
            emotion("sad");
            CHECK(host_lvgl_big_alloc_count() == allocations, "preview opened background decoder");
            if (selector == "preview_double_close") {
                display->SetPreviewImage(nullptr);
                display->SetPreviewImage(nullptr);
            } else
                CHECK(host_esp_timer_fire("preview_timer"), "preview timer not armed");
            CHECK(hidden(display->previewImage()) && !hidden(display->emojiBox()),
                  "preview did not close");
            CHECK(display->gif() && display->gif()->IsPlaying(), "current avatar not restored");
        }
    } else if (selector == "theme_switch_during_speaking" ||
               selector == "collection_replaced_in_place" ||
               selector == "assets_removed_then_restored") {
        handle(display.get(), event_of(ConversationVisualEventKind::TtsStarted, 1000));
        if (selector == "assets_removed_then_restored") {
            theme_named("light")->set_emoji_collection(nullptr);
            display->SetTheme(theme_named("light"));
            CHECK(display->gif() == nullptr, "removed assets kept old decoder");
        }
        auto allocations = host_lvgl_big_alloc_count();
        auto next = make_collection(dir, all_emotions());
        attach_theme(next, "dark");
        attach_theme(next, "light");
        display->SetTheme(
            theme_named(selector == "theme_switch_during_speaking" ? "dark" : "light"));
        CHECK(display->gif() && display->gif()->IsPlaying(), "reload lost speaking");
        CHECK(host_lvgl_big_alloc_count() > allocations, "new assets not reloaded");
        CHECK(hidden(display->statusLabel()), "theme restored clock");
        CHECK(host_lvgl_obj_bg_opa(display->topBar()) == LV_OPA_TRANSP,
              "theme made top strip opaque");
    } else if (selector == "status_bar_indicators" || selector == "low_battery_popup") {
        host_codec_set_volume(0);
        host_board_set_network_icon("HOST-NET");
        host_board_set_battery(selector == "low_battery_popup" ? 0 : 50, false, true);
        display->UpdateStatusBar(false);
        CHECK(std::strlen(text_of(display->muteLabel())) != 0, "mute icon lost");
        CHECK(std::strlen(text_of(display->batteryLabel())) != 0, "battery icon lost");
        if (selector == "low_battery_popup")
            CHECK(!hidden(display->lowBatteryPopup()), "low battery warning lost");
        CHECK(hidden(display->statusLabel()), "status clock visible");
    } else if (selector == "lock_failure_no_mutation") {
        auto allocations = host_lvgl_big_alloc_count();
        host_lvgl_reset_log();
        host_lvgl_port_fail_next_locks(100);
        emotion("sad");
        CHECK(host_lvgl_big_alloc_count() == allocations, "failed lock allocated decoder");
        CHECK(host_lvgl_unlocked_mutation_count() == 0, "graphics mutated without lock");
        host_lvgl_port_fail_next_locks(0);
    } else if (selector == "destruction_with_pending_timers") {
        display->ShowNotification("TEMP", 20);
        display->SetPreviewImage(make_preview_image(dir));
        display.reset();
        CHECK(host_esp_timer_live_count("preview_timer") == 0, "preview timer survived owner");
        CHECK(host_esp_timer_live_count("elena_notification_expiry") == 0,
              "notification timer survived owner");
    } else {
        // All failure cases use a new collection, so no stale borrowed assets.
        std::vector<std::string> keys;
        if (selector == "mutated_resource")
            keys = {"happy#mutated"};
        if (selector == "corrupt_resource")
            keys = {"happy#zero"};
        if (selector == "invalid_resource")
            keys = {"happy#notgif"};
        if (selector == "missing_resource")
            keys = {"neutral"};
        if (selector == "oom_resource")
            keys = all_emotions();
        if (selector == "recover_after_reload")
            keys = {};
        if (selector == "truncated_resource")
            keys = {"happy#truncated"};
        auto broken = make_collection(dir, keys);
        attach_theme(broken, "light");
        if (selector == "null_collection")
            theme_named("light")->set_emoji_collection(nullptr);
        if (selector == "oom_resource")
            host_lvgl_fail_next_allocations(10);
        display->SetTheme(theme_named("light"));
        emotion("happy");
        if (selector != "missing_resource") {
            CHECK(display->gif() == nullptr, "unavailable GIF marked successful");
            auto src = display->emoji_src();
            CHECK(src && src->header.cf == LV_COLOR_FORMAT_RGB565 && src->data_size == 153600,
                  "static fallback absent");
        } else
            CHECK(display->gif() && display->gif()->IsLoaded(), "neutral retry absent");
        auto allocations = host_lvgl_big_alloc_count();
        for (int i = 0; i < 10; ++i)
            tick();
        CHECK(host_lvgl_big_alloc_count() == allocations, "fallback retried each tick");
        CHECK(hidden(display->emojiLabel()), "font emoji fallback surfaced");
        host_lvgl_fail_next_allocations(0);
        if (selector == "recover_after_reload") {
            attach_theme(collection, "light");
            display->SetTheme(theme_named("light"));
            CHECK(display->gif() && display->gif()->IsLoaded(), "new collection could not recover");
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <selector> <assets_dir>\n", argv[0]);
        return 2;
    }
    const std::string selector = argv[1];
    const std::string dir = argv[2];

    int result = 1;
    if (selector == "hidden_conversation") {
        result = case_hidden_conversation(dir);
    } else {
        result = case_behavior(selector, dir);
    }

    if (result == 0 && host_lvgl_nested_lock_rejections() != 0) {
        std::fprintf(stderr, "production code attempted a nested display lock\n");
        result = 1;
    }
    if (result == 0) {
        std::printf("PASS %s\n", selector.c_str());
    }
    return result;
}
