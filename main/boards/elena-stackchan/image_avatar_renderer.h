#ifndef IMAGE_AVATAR_RENDERER_H
#define IMAGE_AVATAR_RENDERER_H

#include "avatar_renderer.h"

// Shows one still image per emotion, centered. Without art it shows the avatar's name.
class ImageAvatarRenderer final : public AvatarRenderer {
public:
    explicit ImageAvatarRenderer(const AvatarArtSource& art);

    void Attach(lv_obj_t* layer) override;
    void Present(const AvatarPresentation& presentation) override;
    void SetPowerSave(bool on) override;

    lv_obj_t* image() const { return image_; }
    lv_obj_t* label() const { return label_; }

    // The collection's name for an emotion that AvatarController produces.
    static const char* MapEmotion(const char* emotion);

private:
    void Show(const char* emotion);

    const AvatarArtSource& art_;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* label_ = nullptr;
    const lv_img_dsc_t* shown_ = nullptr;
    const char* last_emotion_ = "neutral";
    bool power_save_ = false;
};

#endif  // IMAGE_AVATAR_RENDERER_H
