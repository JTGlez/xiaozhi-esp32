#ifndef GIF_AVATAR_RENDERER_H
#define GIF_AVATAR_RENDERER_H

#include "avatar_renderer.h"
#include "gif/lvgl_gif.h"
#include "lvgl_image.h"

#include <array>
#include <memory>

// Plays one 320x240 GIF per emotion, without scaling. One decoder is open at a
// time. GIFs are checked against the integrity data generated at build time; an
// unusable one falls back once to neutral, then to a static image in flash.
class GifAvatarRenderer final : public AvatarRenderer {
public:
    static constexpr size_t kMaxValidatedGifs = 16;

    explicit GifAvatarRenderer(const AvatarArtSource& art);
    ~GifAvatarRenderer() override;

    void Attach(lv_obj_t* layer) override;
    void Present(const AvatarPresentation& presentation) override;
    void SetPowerSave(bool on) override;

    LvglGif* gif() const { return gif_.get(); }
    lv_obj_t* image() const { return image_; }

private:
    void Show(const char* emotion);
    void RecordApplied(const char* emotion, const EmojiCollection* collection);
    void StartGifIfStopped();
    // Returns the collection entry only if it is a usable GIF resource.
    const LvglImage* GetValidGif(const char* emotion);
    bool TryExpression(const char* emotion);
    void ClearExpressionLocked();
    void ShowStaticFallbackLocked();

    const AvatarArtSource& art_;
    lv_obj_t* image_ = nullptr;
    std::unique_ptr<LvglGif> gif_;
    lv_img_dsc_t fallback_dsc_{};

    const char* last_emotion_ = "neutral";
    bool power_save_ = false;

    // GIFs whose checksum already passed in the current collection; the checksum
    // walks the whole file, too slow for every expression change.
    const EmojiCollection* validated_collection_ = nullptr;
    std::array<const lv_img_dsc_t*, kMaxValidatedGifs> validated_gifs_{};
    size_t validated_count_ = 0;

    // What is on screen. Identity is the exact descriptor plus the collection
    // instance, never the emotion alone: a reload brings a new collection.
    const char* applied_emotion_ = nullptr;
    bool applied_valid_ = false;
    bool applied_via_gif_ = false;
    const EmojiCollection* applied_collection_ = nullptr;
    const lv_img_dsc_t* applied_identity_ = nullptr;

    // An emotion known to be unshowable in this collection goes straight to the
    // static fallback instead of reopening a decoder on every change.
    const char* failed_emotion_ = nullptr;
    const EmojiCollection* failed_collection_ = nullptr;
};

#endif  // GIF_AVATAR_RENDERER_H
