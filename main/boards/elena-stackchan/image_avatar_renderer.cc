#include "image_avatar_renderer.h"

#include "avatar_pack_config.h"
#include "lvgl_image.h"

#include <cstring>

ImageAvatarRenderer::ImageAvatarRenderer(const AvatarArtSource& art) : art_(art) {}

const char* ImageAvatarRenderer::MapEmotion(const char* emotion) {
    static constexpr const char* kTable[][2] = {
        {"happy", "happy"},  {"sad", "sad"},    {"thinking", "thinking"},
        {"sleeping", "sleepy"}, {"pet", "loving"}, {"shake", "confused"},
    };
    for (const auto& row : kTable) {
        if (std::strcmp(emotion, row[0]) == 0) {
            return row[1];
        }
    }
    return "neutral";
}

void ImageAvatarRenderer::Attach(lv_obj_t* layer) {
    image_ = lv_img_create(layer);
    lv_obj_center(image_);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    label_ = lv_label_create(layer);
    lv_label_set_text(label_, "");
    lv_obj_center(label_);
    lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
}

void ImageAvatarRenderer::Present(const AvatarPresentation& presentation) {
    if (image_ == nullptr) {
        return;
    }
    last_emotion_ = presentation.emotion;
    if (!power_save_) {
        Show(last_emotion_);
    }
}

void ImageAvatarRenderer::SetPowerSave(bool on) {
    if (image_ == nullptr) {
        return;
    }
    power_save_ = on;
    Show(on ? "sleeping" : last_emotion_);
}

void ImageAvatarRenderer::Show(const char* emotion) {
    auto collection = art_.Collection();
    const LvglImage* image = nullptr;
    if (collection != nullptr) {
        image = collection->GetEmojiImage(MapEmotion(emotion));
        if (image == nullptr) {
            image = collection->GetEmojiImage("neutral");
        }
    }
    if (image == nullptr) {
        shown_ = nullptr;
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_, AVATAR_DISPLAY_NAME);
        lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const lv_img_dsc_t* dsc = image->image_dsc();
    if (dsc == shown_) {
        return;
    }
    shown_ = dsc;
    lv_image_set_src(image_, dsc);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
}
