/* The real AvatarStage driving a fake renderer: the contract, without any art. */
#include "avatar_stage.h"

#include "avatar_pack_config.h"
#include "boot_screen.h"
#include "emoji_collection.h"
#include "assets/lang_config.h"
#include "esp_lvgl_port.h"
#include "fake_avatar_factory.h"
#include "lvgl.h"
#include "lvgl_image.h"
#include "lvgl_theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
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

extern "C" {
extern const lv_image_dsc_t avatar_boot_image;
extern const lv_image_dsc_t avatar_boot_name;
const lv_image_dsc_t avatar_boot_image{};
const lv_image_dsc_t avatar_boot_name{};
}

namespace {

std::string g_dir;

class StageHarness : public AvatarStage {
public:
    using AvatarStage::AvatarStage;
    lv_obj_t* emojiBox() { return emoji_box_; }
    lv_obj_t* previewImage() { return preview_image_; }
    lv_obj_t* topBar() { return top_bar_; }
    lv_obj_t* tail(size_t index) { return tail_[index]; }
    lv_obj_t* statusBubble() { return status_bubble_; }
    lv_obj_t* statusLabel() { return status_label_; }
    BootScreen* boot() { return boot_screen(); }
};

std::unique_ptr<StageHarness> make_display() {
    static int io_stub = 0x10;
    static int panel_stub = 0x20;
    return std::make_unique<StageHarness>(reinterpret_cast<esp_lcd_panel_io_handle_t>(&io_stub),
                                          reinterpret_cast<esp_lcd_panel_handle_t>(&panel_stub),
                                          320, 240, 0, 0, false, false, false);
}

LvglTheme* theme_named(const char* name) { return LvglThemeManager::GetInstance().GetTheme(name); }

std::shared_ptr<EmojiCollection> new_collection() {
    static std::vector<std::shared_ptr<EmojiCollection>> registry;
    registry.push_back(std::make_shared<EmojiCollection>());
    return registry.back();
}

std::unique_ptr<LvglImage> make_preview_image() {
    static std::vector<uint8_t> data;
    if (data.empty()) {
        std::ifstream file(g_dir + "/neutral.gif", std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        CHECK(!data.empty(), "preview asset missing");
    }
    return std::make_unique<LvglRawImage>(data.data(), data.size());
}

ConversationVisualEvent event_of(ConversationVisualEventKind kind, uint64_t now) {
    ConversationVisualEvent event{};
    event.kind = kind;
    event.now_ms = now;
    event.activity = ConversationActivity::Idle;
    event.playback_idle = true;
    return event;
}

/* A stage still on its boot screen. */
std::unique_ptr<StageHarness> booting_stage() {
    host_set_clock_ms(1000);
    auto display = make_display();
    theme_named("light")->set_emoji_collection(new_collection());
    display->SetupUI();
    return display;
}

/* A stage that has finished booting. */
std::unique_ptr<StageHarness> ready_stage() {
    auto display = booting_stage();
    display->OnBootProgress(BootStep::kReady, 0);
    return display;
}

bool hidden(lv_obj_t* obj) { return host_lvgl_obj_hidden(obj); }
bool tree_has(const char* text) { return host_lvgl_tree_has_text(lv_screen_active(), text); }
const char* text_of(lv_obj_t* obj) { return host_lvgl_obj_text(obj); }

size_t presents() { return fake_renderer_log().presentations.size(); }
const char* last_emotion() { return fake_renderer_log().presentations.back().emotion; }

void attach_once() {
    auto display = ready_stage();
    display->SetupUI();
    auto& log = fake_renderer_log();
    CHECK(log.attach_count == 1, "attached more than once");
    CHECK(log.layer == display->emojiBox(), "attached to the wrong layer");
    CHECK(host_lvgl_obj_w(log.layer) == 320 && host_lvgl_obj_h(log.layer) == 240,
          "layer does not fill the screen");
}

void present_only_on_changes() {
    auto display = ready_stage();
    auto before = presents();
    for (int i = 0; i < 20; ++i) {
        display->SetEmotion("happy");
        display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::Tick, 1000));
    }
    CHECK(presents() == before + 1, "repeated presentation reached the renderer");
    CHECK(std::strcmp(last_emotion(), "happy") == 0, "wrong emotion");
}

void present_on_notice_flag_change() {
    auto display = ready_stage();
    auto before = presents();
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeStarted, 1000));
    CHECK(presents() == before + 1, "notice flag did not present");
    CHECK(fake_renderer_log().presentations.back().notice_active, "flag lost");
}

void theme_reload_presents_again() {
    auto display = ready_stage();
    auto before = presents();
    theme_named("light")->set_emoji_collection(new_collection());
    display->SetTheme(theme_named("light"));
    CHECK(presents() == before + 1, "new collection did not present");
}

void same_collection_theme_change_may_present_once() {
    auto display = ready_stage();
    auto before = presents();
    display->SetTheme(theme_named("light"));
    CHECK(presents() <= before + 1, "theme change presented more than once");
}

void preview_blocks_present() {
    auto display = ready_stage();
    display->SetPreviewImage(make_preview_image());
    CHECK(!host_lvgl_obj_hidden(display->previewImage()), "preview not shown");
    auto before = presents();
    display->SetEmotion("sad");
    CHECK(presents() == before, "preview let a presentation through");
    display->SetPreviewImage(nullptr);
    CHECK(presents() == before + 1, "closing the preview did not present");
    CHECK(std::strcmp(last_emotion(), "sad") == 0, "closing showed an old emotion");
}

void power_save_forwarded() {
    auto display = ready_stage();
    auto before = presents();
    auto& log = fake_renderer_log();
    log.power_save.clear();
    display->SetPowerSaveMode(true);
    display->SetPowerSaveMode(false);
    CHECK((log.power_save == std::vector<bool>{true, false}), "power save not forwarded");
    CHECK(presents() == before, "power save presented");
    display->SetEmotion("neutral");
    CHECK(presents() == before, "power save changed the conversation state");
}

void failed_lock_calls_nothing() {
    auto display = ready_stage();
    auto& log = fake_renderer_log();
    log.power_save.clear();
    auto before = presents();
    host_lvgl_port_fail_next_locks(1000);
    display->SetEmotion("sad");
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeStarted, 1000));
    display->SetPowerSaveMode(true);
    host_lvgl_port_fail_next_locks(0);
    CHECK(presents() == before, "renderer called without the lock");
    CHECK(log.power_save.empty(), "power save forwarded without the lock");
}

void renderer_gets_no_text() {
    // Proven at compile time: FakeRenderer is final and overrides only the contract's
    // three methods, none of which takes text.
    auto display = ready_stage();
    auto before = presents();
    display->SetStatus("STATUS-TEXT");
    display->SetChatMessage("assistant", "SPOKEN");
    CHECK(presents() == before, "text reached the renderer");
}

void boot_shows_wifi_first() {
    auto display = booting_stage();
    CHECK(display->boot() != nullptr, "no boot screen");
    CHECK(std::strcmp(text_of(display->boot()->bubble_label()), "Conectando a Wi-Fi...") == 0,
          "wrong first step");
    CHECK(std::strcmp(text_of(display->boot()->version()), "v2.5.0") == 0, "wrong version");
    CHECK(hidden(display->boot()->countdown()), "countdown visible");
    CHECK(hidden(display->boot()->note()), "note visible");
    CHECK(host_lvgl_obj_img_src(display->boot()->image()) == &avatar_boot_image, "wrong image");
    CHECK(host_lvgl_obj_img_src(display->boot()->name()) == &avatar_boot_name, "wrong name");
    CHECK(host_lvgl_obj_bg_color(display->boot()->root()) == AVATAR_BACKGROUND_COLOR,
          "wrong background");
    CHECK(host_lvgl_obj_bg_opa(display->boot()->root()) == LV_OPA_COVER, "root not opaque");
}

void boot_hides_ui() {
    auto display = booting_stage();
    CHECK(hidden(display->topBar()), "top bar visible");
    CHECK(hidden(display->emojiBox()), "layer visible");
    for (size_t i = 0; i < 4; ++i) {
        CHECK(hidden(display->tail(i)), "tail visible");
    }
}

void no_attach_before_ready() {
    auto display = booting_stage();
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::Tick, 1000));
    display->SetEmotion("happy");
    display->OnBootProgress(BootStep::kFindingServer, 0);
    display->OnBootProgress(BootStep::kServerFailed, 8);
    display->OnBootProgress(BootStep::kFindingServer, 0);
    CHECK(fake_renderer_log().attach_count == 0, "attached before ready");
    CHECK(presents() == 0, "presented before ready");
}

void boot_steps_update_the_bubble() {
    auto display = booting_stage();
    auto* boot = display->boot();
    display->OnBootProgress(BootStep::kFindingServer, 0);
    CHECK(std::strcmp(text_of(boot->bubble_label()), "Buscando al servidor...") == 0, "finding");
    CHECK(host_lvgl_obj_bg_color(boot->bubble()) == AVATAR_ACCENT_COLOR, "finding color");
    display->OnBootProgress(BootStep::kServerFailed, 8);
    CHECK(std::strcmp(text_of(boot->bubble_label()), "\xC3\x97 No encuentro el servidor") == 0,
          "failed");
    CHECK(host_lvgl_obj_bg_color(boot->bubble()) == kAvatarErrorColor, "failure color");
    CHECK(host_lvgl_obj_text_color(boot->bubble_label()) == 0xFFFFFF, "failure text color");
    CHECK(std::strcmp(text_of(boot->countdown()), "Reintento en 8 s") == 0, "countdown");
    CHECK(!hidden(boot->countdown()), "countdown hidden");
    display->OnBootProgress(BootStep::kFindingServer, 0);
    CHECK(std::strcmp(text_of(boot->bubble_label()), "Buscando al servidor...") == 0, "again");
    CHECK(host_lvgl_obj_bg_color(boot->bubble()) == AVATAR_ACCENT_COLOR, "stayed red");
    CHECK(host_lvgl_obj_text_color(boot->bubble_label()) == AVATAR_ACCENT_TEXT_COLOR,
          "text stayed white");
    CHECK(hidden(boot->countdown()), "countdown stayed");
}

void failure_then_ready_loads_avatar() {
    auto display = booting_stage();
    display->OnBootProgress(BootStep::kServerFailed, 8);
    auto objects = host_lvgl_live_obj_count();
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(fake_renderer_log().attach_count == 1, "not attached");
    CHECK(display->boot() == nullptr, "boot screen kept");
    CHECK(host_lvgl_live_obj_count() < objects, "boot objects survived");
    CHECK(!hidden(display->topBar()), "top bar hidden");
    CHECK(!hidden(display->emojiBox()), "layer hidden");
    CHECK(fake_renderer_log().layer == display->emojiBox(), "wrong layer");
}

void ready_applies_the_latest_presentation() {
    auto display = booting_stage();
    display->SetEmotion("happy");
    CHECK(presents() == 0, "presented before ready");
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(presents() == 1, "ready did not present once");
    CHECK(std::strcmp(fake_renderer_log().presentations[0].emotion, "happy") == 0, "wrong one");
}

void ready_twice_is_harmless() {
    auto display = booting_stage();
    display->OnBootProgress(BootStep::kReady, 0);
    auto before = presents();
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(fake_renderer_log().attach_count == 1, "attached twice");
    CHECK(presents() == before, "presented again");
}

void progress_after_ready_is_ignored() {
    auto display = ready_stage();
    auto objects = host_lvgl_live_obj_count();
    display->OnBootProgress(BootStep::kServerFailed, 8);
    display->OnBootProgress(BootStep::kFindingServer, 0);
    CHECK(display->boot() == nullptr, "boot screen came back");
    CHECK(host_lvgl_live_obj_count() == objects, "objects created after ready");
    CHECK(!tree_has("No encuentro"), "failure shown after ready");
}

void user_agent_not_shown() {
    auto display = booting_stage();
    auto activity = event_of(ConversationVisualEventKind::Activity, 1000);
    activity.activity = ConversationActivity::Maintenance;
    display->OnConversationVisualEvent(activity);
    display->SetChatMessage("system", "UA-STRING");
    CHECK(!tree_has("UA-STRING"), "user agent on the boot screen");
    display->SetStatus("MAINTENANCE-STATUS");
    CHECK(!tree_has("MAINTENANCE-STATUS"), "maintenance replaced the step");
    CHECK(std::strcmp(text_of(display->boot()->bubble_label()), "Conectando a Wi-Fi...") == 0,
          "step lost");
}

void notice_during_boot_shows_note() {
    auto display = booting_stage();
    auto* boot = display->boot();
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeStarted, 1000));
    display->SetStatus("ST");
    display->SetChatMessage("system", "LONG-HINT");
    CHECK(std::strcmp(text_of(boot->bubble_label()), "ST") == 0, "status not in the bubble");
    CHECK(std::strcmp(text_of(boot->note()), "LONG-HINT") == 0, "hint not in the note");
    CHECK(!hidden(boot->note()), "note hidden");
    CHECK(hidden(boot->image()) && hidden(boot->name()), "art covers the note");
    CHECK(host_lvgl_obj_y(boot->bubble()) == 8, "bubble not moved up");
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeCleared, 1100));
    CHECK(hidden(boot->note()), "note stayed");
    CHECK(!hidden(boot->image()) && !hidden(boot->name()), "art not restored");
    CHECK(host_lvgl_obj_y(boot->bubble()) == 181, "bubble not restored");
}

void error_alert_during_boot_uses_error_color() {
    auto display = booting_stage();
    auto* boot = display->boot();
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeStarted, 1000));
    display->SetStatus(Lang::Strings::ERROR);
    CHECK(std::strcmp(text_of(boot->bubble_label()), "\xC3\x97 XZ_STR_ERROR") == 0, "no mark");
    CHECK(host_lvgl_obj_bg_color(boot->bubble()) == kAvatarErrorColor, "not red");
    CHECK(host_lvgl_obj_text_color(boot->bubble_label()) == 0xFFFFFF, "text not white");
}

void notification_during_boot_hidden() {
    auto display = booting_stage();
    display->ShowNotification("Conectado a MI-RED");
    CHECK(!tree_has("MI-RED"), "the network's name reached the screen");
    display->ShowNotification(std::string("Conectado a OTRA-RED"));
    CHECK(!tree_has("OTRA-RED"), "the network's name reached the screen");
}

void preview_during_boot_ignored() {
    auto display = booting_stage();
    display->SetPreviewImage(make_preview_image());
    CHECK(hidden(display->previewImage()), "preview shown during boot");
    display->SetPreviewImage(nullptr);
    CHECK(hidden(display->previewImage()), "preview shown during boot");
}

void power_save_during_boot() {
    auto display = booting_stage();
    display->SetPowerSaveMode(true);
    CHECK(fake_renderer_log().power_save.empty(), "power save reached a renderer before ready");
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK((fake_renderer_log().power_save == std::vector<bool>{true}), "power save not forwarded");
    display->SetPowerSaveMode(false);
    CHECK((fake_renderer_log().power_save == std::vector<bool>{true, false}), "not forwarded");
}

void failed_lock_during_progress_does_nothing() {
    auto display = booting_stage();
    host_lvgl_port_fail_next_locks(1000);
    display->OnBootProgress(BootStep::kServerFailed, 8);
    display->OnBootProgress(BootStep::kReady, 0);
    host_lvgl_port_fail_next_locks(0);
    CHECK(fake_renderer_log().attach_count == 0, "attached without the lock");
    CHECK(display->boot() != nullptr, "boot screen deleted without the lock");
    CHECK(std::strcmp(text_of(display->boot()->bubble_label()), "Conectando a Wi-Fi...") == 0,
          "bubble changed without the lock");
}

void error_during_boot_shows_after_ready() {
    auto display = booting_stage();
    display->SetStatus(Lang::Strings::ERROR);
    CHECK(std::strcmp(text_of(display->boot()->bubble_label()), "Conectando a Wi-Fi...") == 0,
          "a status without a notice replaced the step");
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(!hidden(display->statusBubble()), "the error vanished at ready");
    CHECK(std::strcmp(text_of(display->statusLabel()), "\xC3\x97 XZ_STR_ERROR") == 0, "no mark");
    CHECK(host_lvgl_obj_bg_color(display->statusBubble()) == kAvatarErrorColor, "not red");
    for (size_t i = 0; i < 4; ++i) {
        CHECK(!hidden(display->tail(i)), "tail hidden");
    }
    display->SetStatus("OK");
    CHECK(host_lvgl_obj_bg_color(display->statusBubble()) == AVATAR_ACCENT_COLOR, "stayed red");
}

void error_in_a_notice_during_boot_shows_after_ready() {
    auto display = booting_stage();
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeStarted, 1000));
    display->SetStatus(Lang::Strings::ERROR);
    display->OnConversationVisualEvent(event_of(ConversationVisualEventKind::NoticeCleared, 1100));
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(!hidden(display->statusBubble()), "the error vanished at ready");
}

void later_status_clears_a_boot_error() {
    auto display = booting_stage();
    display->SetStatus(Lang::Strings::ERROR);
    display->SetStatus("LOADING");
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(hidden(display->statusBubble()), "an error that passed stayed");
    CHECK(!tree_has("XZ_STR_ERROR"), "an error that passed stayed");
}

void ordinary_status_during_boot_is_not_kept() {
    auto display = booting_stage();
    display->SetStatus("LOADING");
    display->OnBootProgress(BootStep::kReady, 0);
    CHECK(hidden(display->statusBubble()), "a routine boot status reached the bubble");
}

void requires_server_at_boot() {
    auto display = booting_stage();
    CHECK(display->RequiresServerAtBoot(), "the stage must wait for the server");
}

const std::map<std::string, std::function<void()>>& cases() {
    static const std::map<std::string, std::function<void()>> all = {
#define CASE(name) {#name, name}
        CASE(attach_once),
        CASE(present_only_on_changes),
        CASE(present_on_notice_flag_change),
        CASE(theme_reload_presents_again),
        CASE(same_collection_theme_change_may_present_once),
        CASE(preview_blocks_present),
        CASE(power_save_forwarded),
        CASE(failed_lock_calls_nothing),
        CASE(renderer_gets_no_text),
        CASE(boot_shows_wifi_first),
        CASE(boot_hides_ui),
        CASE(no_attach_before_ready),
        CASE(boot_steps_update_the_bubble),
        CASE(failure_then_ready_loads_avatar),
        CASE(ready_applies_the_latest_presentation),
        CASE(ready_twice_is_harmless),
        CASE(progress_after_ready_is_ignored),
        CASE(user_agent_not_shown),
        CASE(notice_during_boot_shows_note),
        CASE(error_alert_during_boot_uses_error_color),
        CASE(notification_during_boot_hidden),
        CASE(preview_during_boot_ignored),
        CASE(power_save_during_boot),
        CASE(failed_lock_during_progress_does_nothing),
        CASE(requires_server_at_boot),
        CASE(error_during_boot_shows_after_ready),
        CASE(error_in_a_notice_during_boot_shows_after_ready),
        CASE(later_status_clears_a_boot_error),
        CASE(ordinary_status_during_boot_is_not_kept),
#undef CASE
    };
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3 || cases().find(argv[1]) == cases().end()) {
        std::fprintf(stderr, "usage: %s <case> <gifs_dir>\n", argv[0]);
        return 2;
    }
    g_dir = argv[2];
    cases().at(argv[1])();
    if (host_lvgl_nested_lock_rejections() != 0) {
        std::fprintf(stderr, "production code attempted a nested display lock\n");
        return 1;
    }
    std::printf("PASS %s\n", argv[1]);
    return 0;
}
