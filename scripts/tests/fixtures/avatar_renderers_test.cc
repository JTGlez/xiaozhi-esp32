/* Host harness for the avatar renderers: real renderers, real gifdec, stub LVGL.
 *
 * Each selector prints PASS <name>. The caller holds the display lock in
 * production; these renderers take none, so nothing here takes one either.
 */
#include "avatar_pack_config.h"
#include "avatar_renderer.h"
#include "emoji_collection.h"
#include "gif_avatar_renderer.h"
#include "image_avatar_renderer.h"
#include "lvgl.h"
#include "lvgl_image.h"

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

std::vector<uint8_t>* asset_bytes(const std::string& key) {
    static std::map<std::string, std::vector<uint8_t>> cache;
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
    std::ifstream file(g_dir + "/" + name + ".gif", std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    if (file.fail() || data.empty()) {
        std::fprintf(stderr, "missing asset: %s/%s.gif\n", g_dir.c_str(), name.c_str());
        std::exit(2);
    }
    if (mode == "mutated") {
        data[data.size() - 5] ^= 1;
    } else if (mode == "zero") {
        data[6] = data[7] = data[8] = data[9] = 0;
    } else if (mode == "truncated") {
        data.resize(15);
    } else if (mode == "notgif") {
        data[0] = 'X';
    }
    return &cache.emplace(key, std::move(data)).first->second;
}

std::vector<std::string> all_emotions() {
    return {"email", "happy", "listening", "look_left", "look_right", "neutral",
            "pet",   "sad",   "shake",     "sleeping",  "speaking",   "thinking"};
}

/* Live decoders borrow image data from collections, so they stay alive for the process. */
std::shared_ptr<EmojiCollection> keep(std::shared_ptr<EmojiCollection> collection) {
    static std::vector<std::shared_ptr<EmojiCollection>> registry;
    registry.push_back(collection);
    return collection;
}

std::shared_ptr<EmojiCollection> make_collection(const std::vector<std::string>& keys) {
    auto collection = std::make_shared<EmojiCollection>();
    for (const auto& key : keys) {
        auto* data = asset_bytes(key);
        collection->AddEmoji(key.substr(0, key.find('#')),
                             new LvglRawImage(data->data(), data->size()));
    }
    return keep(collection);
}

/* Still images for the image renderer: their bytes are never decoded here. */
std::shared_ptr<EmojiCollection> make_stills(const std::vector<std::string>& names) {
    static uint8_t still[] = {0x89, 'P', 'N', 'G'};
    auto collection = std::make_shared<EmojiCollection>();
    for (const auto& name : names) {
        collection->AddEmoji(name, new LvglRawImage(still, sizeof(still)));
    }
    return keep(collection);
}

class TestArt : public AvatarArtSource {
public:
    std::shared_ptr<EmojiCollection> Collection() const override { return collection; }
    std::shared_ptr<EmojiCollection> collection;
};

AvatarPresentation presentation_of(const char* emotion, bool notice = false) {
    AvatarPresentation presentation;
    presentation.emotion = emotion;
    presentation.notice_active = notice;
    return presentation;
}

lv_obj_t* make_layer() {
    lv_init();
    return lv_obj_create(lv_screen_active());
}

struct GifFixture {
    TestArt art;
    std::unique_ptr<GifAvatarRenderer> renderer;

    explicit GifFixture(std::shared_ptr<EmojiCollection> collection) {
        art.collection = std::move(collection);
        renderer = std::make_unique<GifAvatarRenderer>(art);
        renderer->Attach(make_layer());
    }
    void present(const char* emotion, bool notice = false) {
        renderer->Present(presentation_of(emotion, notice));
    }
    const lv_img_dsc_t* src() const {
        return static_cast<const lv_img_dsc_t*>(host_lvgl_obj_img_src(renderer->image()));
    }
    bool shows_fallback() const {
        auto* s = src();
        return renderer->gif() == nullptr && s != nullptr &&
               s->header.cf == LV_COLOR_FORMAT_RGB565 && s->data_size == 153600;
    }
    std::vector<uint8_t> frame() const {
        auto* s = src();
        CHECK(s != nullptr && s->data != nullptr, "no frame on screen");
        return std::vector<uint8_t>(s->data, s->data + s->data_size);
    }
};

struct ImageFixture {
    TestArt art;
    std::unique_ptr<ImageAvatarRenderer> renderer;

    explicit ImageFixture(std::shared_ptr<EmojiCollection> collection) {
        art.collection = std::move(collection);
        renderer = std::make_unique<ImageAvatarRenderer>(art);
        renderer->Attach(make_layer());
    }
    void present(const char* emotion) { renderer->Present(presentation_of(emotion)); }
    const void* src() const { return host_lvgl_obj_img_src(renderer->image()); }
    const void* still(const char* name) const {
        return art.collection->GetEmojiImage(name)->image_dsc();
    }
    bool shows(const char* name) const {
        return src() == still(name) && !host_lvgl_obj_hidden(renderer->image()) &&
               host_lvgl_obj_hidden(renderer->label());
    }
    bool shows_name() const {
        return host_lvgl_obj_hidden(renderer->image()) &&
               !host_lvgl_obj_hidden(renderer->label()) &&
               std::strcmp(host_lvgl_obj_text(renderer->label()), AVATAR_DISPLAY_NAME) == 0;
    }
};

void gif_attach_centers_without_scaling() {
    GifFixture f(make_collection(all_emotions()));
    CHECK(host_lvgl_obj_scale(f.renderer->image()) == 256, "avatar scaled");
    CHECK(host_lvgl_obj_scale_set_count(f.renderer->image()) == 0, "scale was set");
}

void gif_same_presentation_keeps_decoder() {
    GifFixture f(make_collection(all_emotions()));
    f.present("happy");
    auto* gif = f.renderer->gif();
    auto allocations = host_lvgl_big_alloc_count();
    CHECK(gif != nullptr && gif->IsLoaded(), "first expression missing");
    for (int i = 0; i < 20; ++i) {
        f.present("happy");
    }
    CHECK(f.renderer->gif() == gif, "same expression reopened the decoder");
    CHECK(host_lvgl_big_alloc_count() == allocations, "same expression allocated");
}

void gif_changes_ignore_notice_flags() {
    GifFixture f(make_collection(all_emotions()));
    f.present("happy");
    auto* gif = f.renderer->gif();
    auto allocations = host_lvgl_big_alloc_count();
    f.present("happy", true);
    f.present("happy", false);
    CHECK(f.renderer->gif() == gif, "notice flag reopened the decoder");
    CHECK(host_lvgl_big_alloc_count() == allocations, "notice flag allocated");
}

void gif_new_collection_reapplies() {
    GifFixture f(make_collection(all_emotions()));
    f.present("happy");
    auto allocations = host_lvgl_big_alloc_count();
    f.art.collection = make_collection(all_emotions());
    f.present("happy");
    CHECK(host_lvgl_big_alloc_count() > allocations, "new collection was not applied");
    CHECK(f.renderer->gif() != nullptr && f.renderer->gif()->IsLoaded(), "reload lost the GIF");
}

void gif_missing_emotion_uses_neutral() {
    GifFixture f(make_collection({"neutral"}));
    f.present("happy");
    CHECK(f.renderer->gif() != nullptr && f.renderer->gif()->IsLoaded(), "neutral retry absent");
}

void gif_corrupted_shows_fallback_without_retry() {
    for (const char* mode : {"#mutated", "#zero", "#truncated", "#notgif"}) {
        GifFixture f(make_collection({std::string("happy") + mode}));
        f.present("happy");
        CHECK(f.shows_fallback(), mode);
        auto allocations = host_lvgl_big_alloc_count();
        for (int i = 0; i < 10; ++i) {
            f.present("happy");
        }
        CHECK(host_lvgl_big_alloc_count() == allocations, "fallback retried a decoder");
        CHECK(f.shows_fallback(), "fallback did not stay");
    }
}

void gif_null_collection_shows_fallback() {
    GifFixture f(nullptr);
    f.present("happy");
    CHECK(f.shows_fallback(), "no fallback without a collection");
    auto allocations = host_lvgl_big_alloc_count();
    for (int i = 0; i < 10; ++i) {
        f.present("happy");
    }
    CHECK(host_lvgl_big_alloc_count() == allocations, "null collection opened a decoder");
}

void gif_oom_shows_fallback() {
    GifFixture f(make_collection(all_emotions()));
    host_lvgl_fail_next_allocations(10);
    f.present("happy");
    host_lvgl_fail_next_allocations(0);
    CHECK(f.shows_fallback(), "no fallback when out of memory");
}

void gif_recovers_after_reload() {
    GifFixture f(make_collection({}));
    f.present("happy");
    CHECK(f.shows_fallback(), "broken collection had no fallback");
    f.art.collection = make_collection(all_emotions());
    f.present("happy");
    CHECK(f.renderer->gif() != nullptr && f.renderer->gif()->IsLoaded(), "did not recover");
}

void gif_power_save_shows_sleeping_then_returns() {
    GifFixture f(make_collection(all_emotions()));
    f.present("happy");
    auto happy = f.frame();
    f.renderer->SetPowerSave(true);
    auto sleeping = f.frame();
    CHECK(sleeping != happy, "power save kept the awake look");
    auto allocations = host_lvgl_big_alloc_count();
    f.present("sad");
    CHECK(host_lvgl_big_alloc_count() == allocations, "present opened a decoder in power save");
    CHECK(f.frame() == sleeping, "present replaced the sleeping look");
    f.renderer->SetPowerSave(false);
    CHECK(host_lvgl_big_alloc_count() > allocations, "power save off did not show sad");
    CHECK(f.frame() != sleeping, "power save off stayed asleep");
}

void gif_power_save_without_presentation_returns_to_neutral() {
    GifFixture f(make_collection(all_emotions()));
    f.renderer->SetPowerSave(true);
    auto sleeping = f.frame();
    f.renderer->SetPowerSave(false);
    CHECK(f.renderer->gif() != nullptr && f.frame() != sleeping, "stayed asleep");
}

void gif_before_attach_is_harmless() {
    lv_init();
    TestArt art;
    art.collection = make_collection(all_emotions());
    GifAvatarRenderer renderer(art);
    auto allocations = host_lvgl_big_alloc_count();
    renderer.Present(presentation_of("happy"));
    renderer.SetPowerSave(true);
    renderer.SetPowerSave(false);
    CHECK(renderer.gif() == nullptr, "decoder opened before attach");
    CHECK(host_lvgl_big_alloc_count() == allocations, "allocated before attach");
}

void gif_destructor_stops_timer() {
    host_lvgl_reset_log();
    {
        GifFixture f(make_collection(all_emotions()));
        f.present("happy");
        CHECK(host_lvgl_log_count(HOST_LVGL_EV_TIMER_CREATE) >= 1, "GIF never armed its timer");
    }
    CHECK(host_lvgl_log_count(HOST_LVGL_EV_TIMER_CREATE) ==
              host_lvgl_log_count(HOST_LVGL_EV_TIMER_DELETE),
          "a GIF timer outlived its renderer");
}

void image_known_emotion() {
    ImageFixture f(make_stills({"neutral", "happy"}));
    f.present("happy");
    CHECK(f.shows("happy"), "happy not shown");
}

void image_mapped_emotion() {
    ImageFixture f(make_stills({"neutral", "sleepy"}));
    f.present("sleeping");
    CHECK(f.shows("sleepy"), "sleeping must show sleepy");
}

void image_unknown_uses_neutral() {
    ImageFixture f(make_stills({"neutral", "happy"}));
    f.present("happy");
    f.present("speaking");
    CHECK(f.shows("neutral"), "speaking must show neutral");
    f.present("happy");
    f.present("nonsense");
    CHECK(f.shows("neutral"), "an unknown name must show neutral");
    f.present("sad");
    CHECK(f.shows("neutral"), "a missing image must fall back to neutral");
}

void image_empty_collection_shows_name() {
    ImageFixture f(make_stills({}));
    f.present("happy");
    CHECK(f.shows_name(), "empty collection must show the name");
}

void image_null_collection_shows_name() {
    ImageFixture f(nullptr);
    f.present("happy");
    CHECK(f.shows_name(), "null collection must show the name");
}

void image_recovers_after_reload() {
    ImageFixture f(nullptr);
    f.present("happy");
    f.art.collection = make_stills({"neutral", "happy"});
    f.present("happy");
    CHECK(f.shows("happy"), "did not recover");
}

void image_power_save_then_returns() {
    ImageFixture f(make_stills({"neutral", "happy", "sad", "sleepy"}));
    f.present("happy");
    f.renderer->SetPowerSave(true);
    CHECK(f.shows("sleepy"), "power save must show sleepy");
    f.present("sad");
    CHECK(f.shows("sleepy"), "present replaced the sleeping look");
    f.renderer->SetPowerSave(false);
    CHECK(f.shows("sad"), "power save off must show the latest presentation");
}

void image_table_matches_plan() {
    const std::pair<const char*, const char*> table[] = {
        {"happy", "happy"},     {"sad", "sad"},         {"thinking", "thinking"},
        {"sleeping", "sleepy"}, {"pet", "loving"},      {"shake", "confused"},
        {"speaking", "neutral"}, {"listening", "neutral"}, {"look_left", "neutral"},
        {"look_right", "neutral"}, {"email", "neutral"},  {"anything", "neutral"},
    };
    for (const auto& [input, output] : table) {
        CHECK(std::strcmp(ImageAvatarRenderer::MapEmotion(input), output) == 0, input);
    }
}

void image_before_attach_is_harmless() {
    lv_init();
    TestArt art;
    art.collection = make_stills({"neutral"});
    ImageAvatarRenderer renderer(art);
    renderer.Present(presentation_of("happy"));
    renderer.SetPowerSave(true);
    CHECK(renderer.image() == nullptr && renderer.label() == nullptr, "attached by itself");
}

void image_gif_is_a_still_source() {
    ImageFixture f(make_collection({"neutral"}));
    auto allocations = host_lvgl_big_alloc_count();
    f.present("happy");
    CHECK(f.shows("neutral"), "GIF entry not shown");
    CHECK(host_lvgl_big_alloc_count() == allocations, "image renderer opened a decoder");
}

const std::map<std::string, std::function<void()>>& cases() {
    static const std::map<std::string, std::function<void()>> all = {
#define CASE(name) {#name, name}
        CASE(gif_attach_centers_without_scaling),
        CASE(gif_same_presentation_keeps_decoder),
        CASE(gif_changes_ignore_notice_flags),
        CASE(gif_new_collection_reapplies),
        CASE(gif_missing_emotion_uses_neutral),
        CASE(gif_corrupted_shows_fallback_without_retry),
        CASE(gif_null_collection_shows_fallback),
        CASE(gif_oom_shows_fallback),
        CASE(gif_recovers_after_reload),
        CASE(gif_power_save_shows_sleeping_then_returns),
        CASE(gif_power_save_without_presentation_returns_to_neutral),
        CASE(gif_before_attach_is_harmless),
        CASE(gif_destructor_stops_timer),
        CASE(image_known_emotion),
        CASE(image_mapped_emotion),
        CASE(image_unknown_uses_neutral),
        CASE(image_empty_collection_shows_name),
        CASE(image_null_collection_shows_name),
        CASE(image_recovers_after_reload),
        CASE(image_power_save_then_returns),
        CASE(image_table_matches_plan),
        CASE(image_before_attach_is_harmless),
        CASE(image_gif_is_a_still_source),
#undef CASE
    };
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <case> <gifs_dir>\n", argv[0]);
        return 2;
    }
    g_dir = argv[2];
    auto selected = cases().find(argv[1]);
    if (selected == cases().end()) {
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }
    selected->second();
    std::printf("PASS %s\n", argv[1]);
    return 0;
}
