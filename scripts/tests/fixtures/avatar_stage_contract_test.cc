/* The real AvatarStage driving a fake renderer: the contract, without any art. */
#include "avatar_stage.h"

#include "emoji_collection.h"
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

namespace {

std::string g_dir;

class StageHarness : public AvatarStage {
public:
    using AvatarStage::AvatarStage;
    lv_obj_t* emojiBox() { return emoji_box_; }
    lv_obj_t* previewImage() { return preview_image_; }
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

/* A stage that has finished its setup, with an empty log. */
std::unique_ptr<StageHarness> ready_stage() {
    host_set_clock_ms(1000);
    auto display = make_display();
    theme_named("light")->set_emoji_collection(new_collection());
    display->SetupUI();
    return display;
}

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
