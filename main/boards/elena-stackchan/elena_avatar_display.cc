#include "elena_avatar_display.h"

#include "elena_avatar_fallback.h"
#include "lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <assets/lang_config.h>
#include <material_symbols.h>

#include <cstring>
#include <memory>

static const char* TAG = "ElenaAvatarDisplay";

namespace {
// Reject malformed containers before handing borrowed data to gifdec, whose
// memory-source API has no length argument. No allocation or decoder is needed.
bool ValidAvatarGif(const lv_img_dsc_t* image) {
    if (!image || !image->data || image->data_size < 14)
        return false;
    const uint8_t* b = image->data;
    const size_t n = image->data_size;
    auto u16 = [&](size_t p) { return unsigned(b[p]) | (unsigned(b[p + 1]) << 8); };
    if ((std::memcmp(b, "GIF89a", 6) && std::memcmp(b, "GIF87a", 6)) || u16(6) != 320 ||
        u16(8) != 240 || !(b[10] & 0x80))
        return false;
    const size_t colors = size_t(2) << (b[10] & 7);
    if (b[11] >= colors)
        return false;
    size_t p = 13 + colors * 3;
    if (p > n)
        return false;
    auto blocks = [&]() {
        while (p < n) {
            size_t length = b[p++];
            if (length == 0)
                return true;
            if (length > n - p)
                return false;
            p += length;
        }
        return false;
    };
    bool frame = false;
    while (p < n) {
        uint8_t type = b[p++];
        if (type == 0x3b)
            return frame && p == n;
        if (type == 0x21) {
            if (p == n)
                return false;
            ++p;  // extension label
            if (!blocks())
                return false;
        } else if (type == 0x2c) {
            if (n - p < 9 || u16(p + 4) == 0 || u16(p + 6) == 0 || u16(p) + u16(p + 4) > 320 ||
                u16(p + 2) + u16(p + 6) > 240)
                return false;
            uint8_t flags = b[p + 8];
            p += 9;
            if (flags & 0x80)
                p += 3 * (size_t(2) << (flags & 7));
            if (p >= n || b[p] < 2 || b[p] > 8)
                return false;
            ++p;
            if (!blocks())
                return false;
            frame = true;
        } else
            return false;
    }
    return false;
}

}  // namespace

ElenaAvatarDisplay::ElenaAvatarDisplay(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_handle_t panel, int width, int height,
                                       int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                       bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {
    elena_fallback_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    elena_fallback_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    elena_fallback_dsc_.header.w = ELENA_AVATAR_FALLBACK_WIDTH;
    elena_fallback_dsc_.header.h = ELENA_AVATAR_FALLBACK_HEIGHT;
    elena_fallback_dsc_.header.stride = ELENA_AVATAR_FALLBACK_WIDTH * 2;
    elena_fallback_dsc_.data_size = ELENA_AVATAR_FALLBACK_SIZE;
    // Borrowed static storage from the generated header: the fallback path
    // never allocates or copies a frame.
    elena_fallback_dsc_.data = elena_avatar_neutral_rgb565;
}

ElenaAvatarDisplay::~ElenaAvatarDisplay() {
    // Set first so any base-class teardown callback that routes through our
    // overrides stops re-applying presentations to a dying object.
    destructing_ = true;
    if (notification_expiry_timer_ != nullptr) {
        esp_timer_stop(notification_expiry_timer_);
        esp_timer_delete(notification_expiry_timer_);
        notification_expiry_timer_ = nullptr;
    }
}

uint64_t ElenaAvatarDisplay::NowMs() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

void ElenaAvatarDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI already applied; skipping rebuild");
        return;
    }
    {
        DisplayLockGuard probe(this);
        if (!probe) {
            ESP_LOGE(TAG, "Cannot setup UI without the display lock");
            return;
        }
        Display::SetupUI();
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        if (lvgl_theme == nullptr) {
            ESP_LOGE(TAG, "No theme available for Elena layout");
            return;
        }
        auto screen = lv_screen_active();

        // Screen + full-screen canvas container. The cream background is a
        // constant; themes only change fonts and indicator colors.
        auto text_font = lvgl_theme->text_font()->font();
        auto icon_font = lvgl_theme->icon_font()->font();
        lv_obj_set_style_text_font(screen, text_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(screen, lv_color_hex(kElenaCanvasCream), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
        container_ = lv_obj_create(screen);
        lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(container_, width_, height_);
        lv_obj_set_pos(container_, 0, 0);
        lv_obj_set_style_bg_color(container_, lv_color_hex(kElenaCanvasCream), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(container_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(container_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(container_, 0, LV_PART_MAIN);

        // Avatar fills the exact 320x240 canvas: no scale, no crop.
        emoji_box_ = lv_obj_create(container_);
        lv_obj_set_size(emoji_box_, width_, height_);
        lv_obj_center(emoji_box_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(emoji_box_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(emoji_box_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(emoji_box_, 0, LV_PART_MAIN);

        emoji_image_ = lv_img_create(emoji_box_);
        lv_obj_center(emoji_image_);

        // The base emoji label is never used for Elena; keep it empty and
        // hidden so the original font-emoji path can never surface.
        emoji_label_ = lv_label_create(emoji_box_);
        lv_label_set_text(emoji_label_, "");
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

        // No bottom bar and no chat_message_label_: conversation stays off
        // screen regardless of SetHideSubtitle.
        preview_image_ = lv_img_create(screen);
        lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
        lv_obj_center(preview_image_);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

        // Transparent top strip with battery/mute/network only.
        top_bar_ = lv_obj_create(container_);
        lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
        lv_obj_set_style_align(top_bar_, LV_ALIGN_TOP_MID, LV_PART_MAIN);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(top_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(top_bar_, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(top_bar_, 2, LV_PART_MAIN);
        lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_set_style_bg_color(top_bar_, lvgl_theme->text_color(), LV_PART_MAIN);
        lv_obj_set_style_text_color(top_bar_, lv_color_hex(0x1F2937), LV_PART_MAIN);

        network_label_ = lv_label_create(top_bar_);
        mute_label_ = lv_label_create(top_bar_);
        lv_label_set_text(network_label_, "");
        lv_label_set_text(mute_label_, "");
        lv_obj_set_style_text_font(network_label_, icon_font, LV_PART_MAIN);
        lv_obj_set_style_text_font(mute_label_, icon_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(network_label_, lv_color_hex(0x1F2937), LV_PART_MAIN);
        lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x1F2937), LV_PART_MAIN);

        // Right icons container (local, same structure as the base layout).
        lv_obj_t* right_icons = lv_obj_create(top_bar_);
        lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_remove_flag(right_icons, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(right_icons, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(right_icons, 0, LV_PART_MAIN);

        battery_label_ = lv_label_create(right_icons);
        lv_obj_set_style_text_font(battery_label_, icon_font, LV_PART_MAIN);
        lv_label_set_text(battery_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
        lv_obj_set_style_text_color(battery_label_, lv_color_hex(0x1F2937), LV_PART_MAIN);

        // Operational message strip: starts empty and hidden. Elena starts
        // with the avatar, never with "Initializing" or a clock.
        status_bar_ = lv_obj_create(container_);
        lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
        lv_obj_set_style_align(status_bar_, LV_ALIGN_TOP_MID, LV_PART_MAIN);
        lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(status_bar_, lv_color_hex(kElenaCanvasCream), LV_PART_MAIN);
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(status_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(status_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(status_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(status_bar_, 2, LV_PART_MAIN);

        notification_label_ = lv_label_create(status_bar_);
        lv_obj_set_width(notification_label_, LV_HOR_RES);
        lv_obj_set_style_align(notification_label_, LV_ALIGN_TOP_MID, LV_PART_MAIN);
        lv_label_set_text(notification_label_, "");
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

        status_label_ = lv_label_create(status_bar_);
        lv_obj_set_width(status_label_, LV_HOR_RES);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_align(status_label_, LV_ALIGN_TOP_MID, LV_PART_MAIN);
        lv_label_set_text(status_label_, "");
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

        low_battery_popup_ = lv_obj_create(container_);
        lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, LV_SIZE_CONTENT);
        lv_obj_set_style_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, LV_PART_MAIN);
        lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(0xBE1A1A), LV_PART_MAIN);
        lv_obj_set_style_radius(low_battery_popup_, 10, LV_PART_MAIN);
        low_battery_label_ = lv_label_create(low_battery_popup_);
        lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
        lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(low_battery_label_, lv_color_hex(0xBE1A1A), LV_PART_MAIN);
        lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    }

    ApplyPresentation(controller_.Resolve(NowMs()));
}

void ElenaAvatarDisplay::ApplyElenaLayerLocked() {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    if (lvgl_theme == nullptr) {
        return;
    }
    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr) {
        lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), LV_PART_MAIN);
        lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(screen, lv_color_hex(kElenaCanvasCream), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (container_ != nullptr) {
        lv_obj_set_size(container_, width_, height_);
        lv_obj_set_pos(container_, 0, 0);
        lv_obj_set_style_bg_color(container_, lv_color_hex(kElenaCanvasCream), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(container_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(container_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(container_, 0, LV_PART_MAIN);
    }
    if (top_bar_ != nullptr) {
        // The base SetTheme paints the top strip opaque; Elena keeps it
        // transparent over the avatar.
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, LV_PART_MAIN);
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_color(battery_label_, lv_color_hex(0x1F2937), LV_PART_MAIN);
        }
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_color(network_label_, lv_color_hex(0x1F2937), LV_PART_MAIN);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x1F2937), LV_PART_MAIN);
        }
    }
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(status_bar_, lv_color_hex(kElenaCanvasCream), LV_PART_MAIN);
        lv_obj_set_style_text_color(status_bar_, lv_color_hex(0x1F2937), LV_PART_MAIN);
    }
    if (emoji_label_ != nullptr) {
        lv_label_set_text(emoji_label_, "");
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (operational_text_active_) {
        ReassertOperationalLocked();
    }
}

void ElenaAvatarDisplay::SetTheme(Theme* theme) {
    if (theme == nullptr) {
        return;
    }
    DisplayLockGuard theme_lock(this);
    if (!theme_lock)
        return;
    if (!setup_ui_called_) {
        current_theme_ = theme;
        return;
    }
    LcdDisplay::SetTheme(theme);  // fonts/colors + settings persistence
    {
        DisplayLockGuard lock(this);
        if (lock) {
            ApplyElenaLayerLocked();
        }
    }
    // A reload can reuse the same Theme pointer with a different emoji
    // collection: invalidate both presentation and failure memo so new
    // assets are re-resolved.
    applied_valid_ = false;
    applied_collection_ = nullptr;
    applied_identity_ = nullptr;
    applied_via_gif_ = false;
    failed_emotion_ = nullptr;
    failed_collection_ = nullptr;
    ApplyPresentation(controller_.Resolve(NowMs()));
}

namespace {
bool MatchesAvatarIntegrity(const char* name, const lv_img_dsc_t* image) {
    if (!image || !image->data)
        return false;
    for (const auto& entry : elena_avatar_gif_integrity) {
        if (std::strcmp(name, entry.name) != 0 || image->data_size != entry.size)
            continue;
        uint32_t crc = 0xffffffffu;
        for (size_t i = 0; i < image->data_size; ++i) {
            crc ^= image->data[i];
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        return (crc ^ 0xffffffffu) == entry.crc32;
    }
    return false;
}
}  // namespace

const LvglImage* ElenaAvatarDisplay::GetValidGif(const char* emotion) const {
    if (emotion == nullptr || current_theme_ == nullptr) {
        return nullptr;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    std::shared_ptr<EmojiCollection> collection = lvgl_theme->emoji_collection();
    if (collection == nullptr) {
        return nullptr;
    }
    const LvglImage* image = collection->GetEmojiImage(emotion);
    if (image == nullptr || !MatchesAvatarIntegrity(emotion, image->image_dsc()) ||
        !ValidAvatarGif(image->image_dsc()) || !image->IsGif()) {
        return nullptr;
    }
    return image;
}

void ElenaAvatarDisplay::ClearExpressionLocked() {
    if (gif_controller_) {
        gif_controller_->Stop();
        if (emoji_image_ != nullptr) {
            lv_image_set_src(emoji_image_, nullptr);
        }
        gif_controller_.reset();
    } else if (emoji_image_ != nullptr) {
        lv_image_set_src(emoji_image_, nullptr);
    }
    applied_via_gif_ = false;
    applied_identity_ = nullptr;
}

void ElenaAvatarDisplay::ShowStaticFallbackLocked() {
    if (emoji_image_ == nullptr) {
        return;
    }
    if (gif_controller_) {
        gif_controller_->Stop();
        lv_image_set_src(emoji_image_, nullptr);
        gif_controller_.reset();
    }
    lv_image_set_src(emoji_image_, &elena_fallback_dsc_);
    lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    if (emoji_label_ != nullptr) {
        lv_label_set_text(emoji_label_, "");
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
    applied_via_gif_ = false;
    applied_identity_ = &elena_fallback_dsc_;
}

void ElenaAvatarDisplay::ShowStaticFallback() {
    DisplayLockGuard lock(this);
    if (!lock) {
        ESP_LOGE(TAG, "Static fallback needs the display lock");
        return;
    }
    ShowStaticFallbackLocked();
}

bool ElenaAvatarDisplay::TryExpression(const char* emotion) {
    {
        DisplayLockGuard probe(this);
        if (!probe) {
            return false;
        }
    }
    if (GetValidGif(emotion) == nullptr) {
        return false;
    }
    {
        // Release the previous expression (stop + clear src + release the
        // decoder) before the base layer builds the replacement.
        DisplayLockGuard lock(this);
        if (!lock) {
            return false;
        }
        ClearExpressionLocked();
    }
    // Real production engine path in LcdDisplay::SetEmotion.
    LcdDisplay::SetEmotion(emotion);
    if (gif_controller_ != nullptr && gif_controller_->IsLoaded()) {
        applied_via_gif_ = true;
        applied_identity_ = gif_controller_->image_dsc();
        return true;
    }
    // gif_controller_ is null on open/decode/OOM failure: not proof of
    // success.
    return false;
}

void ElenaAvatarDisplay::ApplyPresentation(const AvatarPresentation& presentation) {
    if (!setup_ui_called_ || destructing_) {
        return;
    }
    {
        DisplayLockGuard probe(this);
        if (!probe) {
            ESP_LOGE(TAG, "Cannot apply presentation without the display lock");
            return;
        }
    }

    DisplayLockGuard presentation_lock(this);
    if (!presentation_lock)
        return;
    bool operational_owner = presentation.notice_active || presentation.maintenance;

    if (preview_open_) {
        if (!operational_owner) {
            // Preview stays open; the current semantic presentation is
            // applied when it closes.
            return;
        }
        // An operational notice has priority over an open preview.
        ClosePreviewWidgets();
    }

    if (operational_owner) {
        if (operational_text_active_) {
            DisplayLockGuard lock(this);
            if (lock) {
                ReassertOperationalLocked();
            }
        }
    } else if (operational_text_active_) {
        DisplayLockGuard lock(this);
        if (lock) {
            ClearOperationalLocked();
        }
        operational_text_active_ = false;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    std::shared_ptr<EmojiCollection> collection =
        lvgl_theme != nullptr ? lvgl_theme->emoji_collection() : nullptr;
    const EmojiCollection* collection_ptr = collection.get();

    // Fast path 1: the same resolved presentation over the same collection
    // instance does not reopen anything.
    if (applied_valid_ && presentation == applied_presentation_ &&
        collection_ptr == applied_collection_) {
        if (applied_via_gif_ && gif_controller_ != nullptr && !gif_controller_->IsPlaying()) {
            DisplayLockGuard lock(this);
            if (lock) {
                gif_controller_->Start();
            }
        }
        return;
    }

    // Fast path 2: the target asset is already the identity on screen (for
    // example a neutral fallback that a later presentation also wants).
    const LvglImage* target = GetValidGif(presentation.emotion);
    if (applied_valid_ && target != nullptr && target->image_dsc() == applied_identity_) {
        applied_presentation_ = presentation;
        applied_collection_ = collection_ptr;
        failed_emotion_ = nullptr;
        failed_collection_ = nullptr;
        if (applied_via_gif_ && gif_controller_ != nullptr && !gif_controller_->IsPlaying()) {
            DisplayLockGuard lock(this);
            if (lock) {
                gif_controller_->Start();
            }
        }
        return;
    }

    // Failed-attempt memoization: a resolved expression already known to be
    // unshowable in this collection goes straight to the static fallback.
    if (failed_emotion_ != nullptr && failed_collection_ == collection_ptr &&
        std::strcmp(failed_emotion_, presentation.emotion) == 0) {
        ShowStaticFallback();
        applied_valid_ = true;
        applied_presentation_ = presentation;
        applied_collection_ = collection_ptr;
        return;
    }

    bool applied = TryExpression(presentation.emotion);
    if (!applied && std::strcmp(presentation.emotion, "neutral") != 0) {
        // Exactly one neutral retry; a failing neutral never retries itself.
        ESP_LOGW(TAG, "Avatar GIF '%s' unavailable, trying neutral fallback", presentation.emotion);
        applied = TryExpression("neutral");
    }
    if (applied) {
        applied_valid_ = true;
        applied_presentation_ = presentation;
        applied_collection_ = collection_ptr;
        failed_emotion_ = nullptr;
        failed_collection_ = nullptr;
        return;
    }

    ESP_LOGE(TAG, "Avatar assets unavailable, using static Elena fallback");
    ShowStaticFallback();
    failed_emotion_ = presentation.emotion;
    failed_collection_ = collection_ptr;
    applied_valid_ = true;
    applied_presentation_ = presentation;
    applied_collection_ = collection_ptr;
}

void ElenaAvatarDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (emotion == nullptr) {
        return;
    }
    const char* normalized = NormalizeAvatarEmotion(emotion);
    uint64_t now = NowMs();
    if (std::strcmp(emotion, "neutral") == 0) {
        // Legacy idle refresh: never erases semantic activity and is never
        // stored as a semantic emotion.
        ApplyPresentation(controller_.Resolve(now));
        return;
    }
    ConversationVisualEvent event{};
    event.kind = ConversationVisualEventKind::Emotion;
    event.generation = controller_.generation();
    event.now_ms = now;
    event.emotion = normalized;
    controller_.Handle(event);
    ApplyPresentation(controller_.Resolve(now));
}

void ElenaAvatarDisplay::OnConversationVisualEvent(const ConversationVisualEvent& event) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    const char* previous_reason = controller_.ExpiryReason();
    controller_.Handle(event);
    const char* reason = controller_.ExpiryReason();
    if (std::strcmp(reason, previous_reason) != 0 && *reason != '\0')
        ESP_LOGI(TAG, "Visual cycle: %s", reason);
    ApplyPresentation(controller_.Resolve(event.now_ms));
}

void ElenaAvatarDisplay::ReassertOperationalLocked() {
    if (status_label_ == nullptr) {
        return;
    }
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    if (notification_label_ != nullptr) {
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ElenaAvatarDisplay::ClearOperationalLocked() {
    if (status_label_ != nullptr) {
        lv_label_set_text(status_label_, "");
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    }
    operational_text_active_ = false;
}

void ElenaAvatarDisplay::SetStatus(const char* status) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (!setup_ui_called_) {
        return;
    }
    if (routine_status_update_) {
        // Clock/idle refresh from the base: never visible on Elena.
        return;
    }
    if (status == nullptr || status[0] == '\0') {
        return;
    }
    AvatarPresentation presentation = controller_.Resolve(NowMs());
    if (!presentation.notice_active && !presentation.maintenance) {
        // Conversation-style status is suppressed; Elena has no bottom bar
        // either.
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        ESP_LOGE(TAG, "Cannot show operational status without the display lock");
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    if (notification_label_ != nullptr) {
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    }
    operational_text_active_ = true;
}

void ElenaAvatarDisplay::SetChatMessage(const char* role, const char* content) {
    // Elena never shows conversation subtitles in either role; role is not
    // inspected so no language text can leak.
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    AvatarPresentation presentation = controller_.Resolve(NowMs());
    bool owner = presentation.notice_active || presentation.maintenance;
    if (owner && role != nullptr && std::strcmp(role, "system") == 0 && content != nullptr &&
        content[0] != '\0') {
        lv_label_set_text(status_label_, content);
        lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        if (notification_label_ != nullptr) {
            lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        }
        operational_text_active_ = true;
    }
}

void ElenaAvatarDisplay::ClearChatMessages() {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    auto presentation = controller_.Resolve(NowMs());
    if (!presentation.notice_active && !presentation.maintenance)
        ClearOperationalLocked();
}

void ElenaAvatarDisplay::SetHideSubtitle(bool hide) {
    // Intentionally a no-op in both directions: Elena never shows
    // conversation text, regardless of the user setting.
    (void)hide;
}

void ElenaAvatarDisplay::EnsureNotificationTimer() {
    if (notification_expiry_timer_ == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = &ElenaAvatarDisplay::OnNotificationExpiry;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "elena_notification_expiry";
        ESP_ERROR_CHECK(esp_timer_create(&args, &notification_expiry_timer_));
    }
}

void ElenaAvatarDisplay::OnNotificationExpiry(void* arg) {
    ElenaAvatarDisplay* self = static_cast<ElenaAvatarDisplay*>(arg);
    DisplayLockGuard lock(self);
    if (!lock) {
        return;
    }
    if (self->notification_label_ != nullptr) {
        lv_obj_add_flag(self->notification_label_, LV_OBJ_FLAG_HIDDEN);
        if (!self->operational_text_active_)
            lv_obj_add_flag(self->status_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    if (self->operational_text_active_ && self->status_label_ != nullptr) {
        // Restore only an operational notice, never status_label_ itself.
        lv_obj_remove_flag(self->status_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ElenaAvatarDisplay::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (!setup_ui_called_ || notification == nullptr) {
        return;
    }
    AvatarPresentation presentation = controller_.Resolve(NowMs());
    if ((presentation.notice_active || presentation.maintenance) && operational_text_active_) {
        ESP_LOGI(TAG, "Operational notice keeps priority over notifications");
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(notification_label_, notification);
    lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    if (status_label_ != nullptr) {
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    }
    EnsureNotificationTimer();
    esp_timer_stop(notification_expiry_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_expiry_timer_, (int64_t)duration_ms * 1000));
}

void ElenaAvatarDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void ElenaAvatarDisplay::ClosePreviewWidgets() {
    if (!preview_open_) {
        // Double close / close without open: safe no-op.
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
    }
    if (preview_image_ != nullptr && emoji_box_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    preview_image_cached_.reset();
    preview_open_ = false;
}

void ElenaAvatarDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "Preview ignored before the UI is set up");
        return;
    }
    if (image != nullptr) {
        AvatarPresentation presentation = controller_.Resolve(NowMs());
        if ((presentation.notice_active || presentation.maintenance) && operational_text_active_) {
            ESP_LOGI(TAG, "Preview blocked by an active operational notice");
            return;
        }
        // Base behavior: cache the image, stop the GIF, hide the emoji box,
        // show the preview and arm the 5 s preview timer.
        LcdDisplay::SetPreviewImage(std::move(image));
        preview_open_ = true;
        return;
    }
    // Close path (explicit or from the registered base preview timer):
    // close widgets, then apply the CURRENT semantic presentation, never a
    // cached old expression.
    ClosePreviewWidgets();
    if (!destructing_) {
        ApplyPresentation(controller_.Resolve(NowMs()));
    }
}

void ElenaAvatarDisplay::UpdateStatusBar(bool update_all) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    // The base may format a clock string and route it through the virtual
    // SetStatus; mark it routine so SetStatus rejects it. Indicators and the
    // low-battery popup keep working.
    routine_status_update_ = true;
    LvglDisplay::UpdateStatusBar(update_all);
    routine_status_update_ = false;
}
