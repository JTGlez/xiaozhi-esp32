#include "boot_screen.h"

#include "avatar_clock.h"
#include "avatar_pack_config.h"
#include "avatar_stage.h"

#include <string>

namespace {
constexpr int kImageY = 8;
constexpr int kNameY = 133;
constexpr int kBubbleY = 181;
constexpr int kBubbleNoteY = 8;
constexpr int kBubblePadHorizontal = 10;
constexpr int kBubblePadVertical = 1;
constexpr int kBubbleMaxTextWidth = 280;
constexpr int kCountdownY = 211;
constexpr int kVersionRightOffset = -8;
constexpr int kVersionBottomOffset = -5;
constexpr int kNoteX = 10;
constexpr int kNoteY = 44;
constexpr int kNoteWidth = 300;

lv_obj_t* CreateLabel(lv_obj_t* parent, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}
}  // namespace

BootScreen::BootScreen(lv_obj_t* parent, const lv_image_dsc_t* image,
                       const lv_image_dsc_t* name_image, const char* version) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(AVATAR_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);

    image_ = lv_img_create(root_);
    lv_image_set_src(image_, image);
    lv_obj_align(image_, LV_ALIGN_TOP_MID, 0, kImageY);

    name_ = lv_img_create(root_);
    lv_image_set_src(name_, name_image);
    lv_obj_align(name_, LV_ALIGN_TOP_MID, 0, kNameY);

    bubble_ = lv_obj_create(root_);
    lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bubble_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(bubble_, LV_ALIGN_TOP_MID, 0, kBubbleY);
    lv_obj_set_style_bg_opa(bubble_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bubble_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bubble_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_left(bubble_, kBubblePadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bubble_, kBubblePadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_top(bubble_, kBubblePadVertical, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(bubble_, kBubblePadVertical, LV_PART_MAIN);
    bubble_label_ = CreateLabel(bubble_, AVATAR_ACCENT_TEXT_COLOR);
    lv_label_set_long_mode(bubble_label_, LV_LABEL_LONG_MODE_DOTS);

    countdown_ = CreateLabel(root_, AVATAR_ACCENT_TEXT_COLOR);
    lv_obj_align(countdown_, LV_ALIGN_TOP_MID, 0, kCountdownY);
    lv_obj_add_flag(countdown_, LV_OBJ_FLAG_HIDDEN);

    version_ = CreateLabel(root_, AVATAR_ICON_COLOR);
    lv_label_set_text(version_, (std::string("v") + version).c_str());
    lv_obj_align(version_, LV_ALIGN_BOTTOM_RIGHT, kVersionRightOffset, kVersionBottomOffset);

    note_ = CreateLabel(root_, AVATAR_ACCENT_TEXT_COLOR);
    lv_obj_set_width(note_, kNoteWidth);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(note_, kNoteX, kNoteY);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

    Show(InitialBootView());
}

BootScreen::~BootScreen() { lv_obj_del(root_); }

void BootScreen::Show(const BootView& view) {
    ShowBubble(view.bubble, view.is_error);
    const std::string countdown = BootCountdownText(view.retry_seconds);
    lv_label_set_text(countdown_, countdown.c_str());
    if (countdown.empty()) {
        lv_obj_add_flag(countdown_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(countdown_, LV_OBJ_FLAG_HIDDEN);
    }
}

void BootScreen::ShowBubble(const char* text, bool is_error) {
    lv_point_t size;
    lv_text_get_size(&size, text, lv_obj_get_style_text_font(bubble_label_, LV_PART_MAIN), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(bubble_label_, AvatarBubbleLabelWidth(size.x, kBubbleMaxTextWidth));
    // A label of content height wraps long text instead of ending it in dots.
    lv_obj_set_height(bubble_label_, size.y);
    lv_label_set_text(bubble_label_, text);
    lv_obj_set_style_bg_color(bubble_, lv_color_hex(is_error ? kAvatarErrorColor : AVATAR_ACCENT_COLOR),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(
        bubble_label_, is_error ? lv_color_white() : lv_color_hex(AVATAR_ACCENT_TEXT_COLOR),
        LV_PART_MAIN);
    if (text[0] == '\0') {
        lv_obj_add_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(bubble_, LV_OBJ_FLAG_HIDDEN);
    }
}

void BootScreen::SetNote(const char* text) {
    const bool has_note = text != nullptr && text[0] != '\0';
    if (!has_note && !has_note_) {
        return;
    }
    has_note_ = has_note;
    if (has_note) {
        lv_label_set_text(note_, text);
        lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(name_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(bubble_, LV_ALIGN_TOP_MID, 0, kBubbleNoteY);
    } else {
        lv_label_set_text(note_, "");
        lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(name_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(bubble_, LV_ALIGN_TOP_MID, 0, kBubbleY);
    }
}
