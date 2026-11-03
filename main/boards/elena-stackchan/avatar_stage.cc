#include "avatar_stage.h"

#include "avatar_clock.h"
#include "avatar_pack_config.h"
#include "boot_screen.h"
#include "lvgl_display/lvgl_theme.h"

#include <assets/lang_config.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <material_symbols.h>

#include <cstring>
#include <ctime>
#include <memory>
#include <string>

static const char* TAG = "AvatarStage";

// Each avatar's folder compiles these from its boot_image.c and boot_name.c.
extern "C" const lv_image_dsc_t avatar_boot_image;
extern "C" const lv_image_dsc_t avatar_boot_name;

namespace {
constexpr int kTopBarHeight = 30;
constexpr int kBubbleX = 4;
constexpr int kBubbleY = 3;
constexpr int kBubblePadHorizontal = 10;
constexpr int kBubblePadVertical = 1;
constexpr int kTailCenterX = 37;
constexpr int kTailTopY = 27;
constexpr int kTailStepHeight = 2;
constexpr int kTailWidths[] = {14, 10, 6, 2};
constexpr int kRightGroupRightOffset = -4;
constexpr int kRightGroupTopOffset = 5;
constexpr int kRightGroupGap = 5;
constexpr int kNoticeX = 4;
constexpr int kNoticeY = 38;
constexpr int kNoticeWidth = 312;
constexpr int kNoticeRadius = 12;
constexpr int kNoticePad = 4;
constexpr int kNoticeTextWidth = 296;
constexpr const char* kErrorMark = "\xC3\x97 ";  // The built-in font has this glyph

// An object with no look of its own: the default theme gives each a card.
lv_obj_t* CreatePlainObject(lv_obj_t* parent) {
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    return object;
}

lv_obj_t* CreateIcon(lv_obj_t* parent, const lv_font_t* font) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(AVATAR_ICON_COLOR), LV_PART_MAIN);
    return label;
}

lv_obj_t* CreateNoticeLabel(lv_obj_t* parent) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, kNoticeTextWidth);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(AVATAR_ACCENT_TEXT_COLOR), LV_PART_MAIN);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}
}  // namespace

AvatarStage::AvatarStage(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y, bool mirror_x,
                         bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

AvatarStage::~AvatarStage() {
    // Set first so any base-class teardown callback that routes through our
    // overrides stops re-applying presentations to a dying object.
    destructing_ = true;
    if (notification_expiry_timer_ != nullptr) {
        esp_timer_stop(notification_expiry_timer_);
        esp_timer_delete(notification_expiry_timer_);
        notification_expiry_timer_ = nullptr;
    }
}

uint64_t AvatarStage::NowMs() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

AvatarPresentation AvatarStage::ResolveNow() {
    std::lock_guard<std::mutex> guard(controller_mutex_);
    return controller_.Resolve(NowMs());
}

std::shared_ptr<EmojiCollection> AvatarStage::Collection() const {
    return current_theme_ ? static_cast<LvglTheme*>(current_theme_)->emoji_collection() : nullptr;
}

void AvatarStage::SetupUI() {
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
        if (current_theme_ == nullptr) {
            ESP_LOGE(TAG, "No theme available for the stage");
            return;
        }
        CreateLayoutLocked();
        lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        boot_ = std::make_unique<BootScreen>(lv_screen_active(), &avatar_boot_image,
                                             &avatar_boot_name, esp_app_get_description()->version);
    }
    ApplyPresentation(ResolveNow());
}

void AvatarStage::OnBootProgress(BootStep step, int retry_seconds) {
    DisplayLockGuard lock(this);
    if (!lock || ready_ || boot_ == nullptr) {
        return;
    }
    if (step != BootStep::kReady) {
        boot_->Show(ResolveBootView(step, retry_seconds));
        if (step == BootStep::kServerFailed) {
            boot_->SetNote("");
        }
        return;
    }
    renderer_ = CreateAvatarRenderer(*this);
    renderer_->Attach(emoji_box_);
    if (power_save_) {
        renderer_->SetPowerSave(true);
    }
    boot_.reset();
    lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
    ready_ = true;
    ApplyPresentation(ResolveNow());
    if (boot_error_pending_) {
        SetStatus(Lang::Strings::ERROR);
    }
}

void AvatarStage::CreateLayoutLocked() {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto screen = lv_screen_active();

    lv_obj_set_style_text_font(screen, text_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen, lv_color_hex(AVATAR_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    container_ = CreatePlainObject(screen);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_pos(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(AVATAR_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, LV_PART_MAIN);

    // The renderer's layer: the whole canvas, below everything else.
    emoji_box_ = CreatePlainObject(container_);
    lv_obj_set_size(emoji_box_, width_, height_);
    lv_obj_center(emoji_box_);

    // Unused, but LcdDisplay::SetTheme writes to it.
    emoji_label_ = lv_label_create(emoji_box_);
    lv_label_set_text(emoji_label_, "");
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    preview_image_ = lv_img_create(screen);
    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
    lv_obj_center(preview_image_);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    top_bar_ = CreatePlainObject(container_);
    lv_obj_set_size(top_bar_, width_, kTopBarHeight);
    lv_obj_set_pos(top_bar_, 0, 0);

    status_bubble_ = CreatePlainObject(top_bar_);
    lv_obj_set_size(status_bubble_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(status_bubble_, kBubbleX, kBubbleY);
    lv_obj_set_style_bg_opa(status_bubble_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(status_bubble_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_left(status_bubble_, kBubblePadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_right(status_bubble_, kBubblePadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_top(status_bubble_, kBubblePadVertical, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(status_bubble_, kBubblePadVertical, LV_PART_MAIN);
    lv_obj_add_flag(status_bubble_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bubble_);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(status_label_, "");

    for (size_t i = 0; i < tail_.size(); ++i) {
        tail_[i] = CreatePlainObject(container_);
        lv_obj_set_size(tail_[i], kTailWidths[i], kTailStepHeight);
        lv_obj_set_pos(tail_[i], kTailCenterX - kTailWidths[i] / 2,
                       kTailTopY + static_cast<int>(i) * kTailStepHeight);
        lv_obj_set_style_bg_opa(tail_[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(tail_[i], LV_OBJ_FLAG_HIDDEN);
    }
    ApplyBubbleColorsLocked();

    right_group_ = CreatePlainObject(top_bar_);
    lv_obj_set_size(right_group_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(right_group_, LV_ALIGN_TOP_RIGHT, kRightGroupRightOffset, kRightGroupTopOffset);
    lv_obj_set_flex_flow(right_group_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_group_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(right_group_, kRightGroupGap, LV_PART_MAIN);

    clock_label_ = lv_label_create(right_group_);
    lv_label_set_text(clock_label_, "");
    lv_obj_set_style_text_color(clock_label_, lv_color_hex(AVATAR_ICON_COLOR), LV_PART_MAIN);
    lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
    network_label_ = CreateIcon(right_group_, icon_font);
    mute_label_ = CreateIcon(right_group_, icon_font);
    battery_label_ = CreateIcon(right_group_, icon_font);
    lv_label_set_text(battery_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);

    status_bar_ = CreatePlainObject(container_);
    lv_obj_set_size(status_bar_, kNoticeWidth, LV_SIZE_CONTENT);
    lv_obj_set_pos(status_bar_, kNoticeX, kNoticeY);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_bar_, lv_color_hex(AVATAR_ACCENT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_radius(status_bar_, kNoticeRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_bar_, kNoticePad, LV_PART_MAIN);
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    notification_label_ = CreateNoticeLabel(status_bar_);
    notice_label_ = CreateNoticeLabel(status_bar_);

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

void AvatarStage::ApplyBubbleColorsLocked() {
    lv_color_t background =
        lv_color_hex(bubble_is_error_ ? kAvatarErrorColor : AVATAR_ACCENT_COLOR);
    lv_obj_set_style_bg_color(status_bubble_, background, LV_PART_MAIN);
    for (lv_obj_t* step : tail_) {
        lv_obj_set_style_bg_color(step, background, LV_PART_MAIN);
    }
    lv_obj_set_style_text_color(
        status_label_, bubble_is_error_ ? lv_color_white() : lv_color_hex(AVATAR_ACCENT_TEXT_COLOR),
        LV_PART_MAIN);
}

void AvatarStage::ApplyStageStyleLocked() {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    if (lvgl_theme == nullptr || container_ == nullptr) {
        return;
    }
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen, lv_color_hex(AVATAR_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_pos(container_, 0, 0);
    lv_obj_set_style_bg_image_src(container_, nullptr, LV_PART_MAIN);
    lv_obj_set_style_bg_color(container_, lv_color_hex(AVATAR_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, LV_PART_MAIN);
    // The base SetTheme paints the top bar and restyles its labels.
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_color_t icon_color = lv_color_hex(AVATAR_ICON_COLOR);
    for (lv_obj_t* label : {clock_label_, network_label_, mute_label_, battery_label_}) {
        lv_obj_set_style_text_color(label, icon_color, LV_PART_MAIN);
    }
    ApplyBubbleColorsLocked();
    lv_color_t accent_text = lv_color_hex(AVATAR_ACCENT_TEXT_COLOR);
    lv_obj_set_style_text_color(notification_label_, accent_text, LV_PART_MAIN);
    lv_obj_set_style_text_color(notice_label_, accent_text, LV_PART_MAIN);
    lv_label_set_text(emoji_label_, "");
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    if (operational_text_active_) {
        ReassertOperationalLocked();
    }
}

void AvatarStage::SetTheme(Theme* theme) {
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
            ApplyStageStyleLocked();
        }
    }
    // A reload can reuse the same Theme pointer with a different collection.
    presented_valid_ = false;
    ApplyPresentation(ResolveNow());
}

void AvatarStage::ApplyPresentation(const AvatarPresentation& presentation) {
    if (!setup_ui_called_ || destructing_) {
        return;
    }
    DisplayLockGuard presentation_lock(this);
    if (!presentation_lock) {
        ESP_LOGE(TAG, "Cannot apply presentation without the display lock");
        return;
    }
    {
        std::lock_guard<std::mutex> guard(controller_mutex_);
        last_requested_ = presentation;
        last_requested_valid_ = true;
    }
    if (!ready_) {
        if (boot_ != nullptr && !presentation.notice_active) {
            boot_->SetNote("");
        }
        return;
    }
    bool operational_owner = presentation.notice_active || presentation.maintenance;

    if (preview_open_) {
        if (!operational_owner) {
            // Preview stays open; the current presentation applies when it closes.
            return;
        }
        // An operational notice has priority over an open preview.
        ClosePreviewWidgets();
    }

    if (operational_owner) {
        if (operational_text_active_) {
            ReassertOperationalLocked();
        }
    } else if (operational_text_active_) {
        ClearOperationalLocked();
    }

    if (renderer_ == nullptr) {
        return;
    }
    auto collection = Collection();
    if (presented_valid_ && presentation == presented_ &&
        collection.get() == presented_collection_) {
        return;
    }
    presented_valid_ = true;
    presented_ = presentation;
    presented_collection_ = collection.get();
    renderer_->Present(presentation);
}

void AvatarStage::SetEmotion(const char* emotion) {
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
        ApplyPresentation(ResolveNow());
        return;
    }
    AvatarPresentation presentation;
    {
        std::lock_guard<std::mutex> guard(controller_mutex_);
        ConversationVisualEvent event{};
        event.kind = ConversationVisualEventKind::Emotion;
        event.generation = controller_.generation();
        event.now_ms = now;
        event.emotion = normalized;
        controller_.Handle(event);
        presentation = controller_.Resolve(now);
    }
    ApplyPresentation(presentation);
}

void AvatarStage::OnConversationVisualEvent(const ConversationVisualEvent& event) {
    AvatarPresentation presentation;
    bool unchanged;
    const char* reason;
    bool reason_changed;
    {
        std::lock_guard<std::mutex> guard(controller_mutex_);
        const char* previous_reason = controller_.ExpiryReason();
        controller_.Handle(event);
        reason = controller_.ExpiryReason();
        reason_changed = std::strcmp(reason, previous_reason) != 0 && *reason != '\0';
        presentation = controller_.Resolve(event.now_ms);
        unchanged = last_requested_valid_ && presentation == last_requested_;
    }
    if (reason_changed)
        ESP_LOGI(TAG, "Visual cycle: %s", reason);
    // This runs at 20 Hz on the main task: without a visible change it must not
    // wait for, and boost, a render holding the LVGL lock.
    if (!unchanged)
        ApplyPresentation(presentation);
}

void AvatarStage::SetPowerSaveMode(bool on) {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    power_save_ = on;
    if (renderer_ != nullptr) {
        renderer_->SetPowerSave(on);
    }
}

void AvatarStage::ReassertOperationalLocked() {
    if (notice_label_ == nullptr || !notice_text_present_) {
        return;
    }
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(notice_label_, LV_OBJ_FLAG_HIDDEN);
    if (notification_label_ != nullptr) {
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AvatarStage::ClearOperationalLocked() {
    if (notice_label_ != nullptr) {
        lv_label_set_text(notice_label_, "");
        lv_obj_add_flag(notice_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    notice_text_present_ = false;
    operational_text_active_ = false;
}

void AvatarStage::SetStatus(const char* status) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (!setup_ui_called_ || status_label_ == nullptr) {
        return;
    }
    if (routine_status_update_) {
        // The base's clock: it has its own label here.
        return;
    }
    if (status == nullptr || status[0] == '\0') {
        return;
    }
    // State text only ("listening", "error"): the conversation never comes this way.
    const bool is_error = std::strcmp(status, Lang::Strings::ERROR) == 0;
    std::string text = is_error ? std::string(kErrorMark) + status : std::string(status);
    if (!ready_) {
        boot_error_pending_ = is_error;
        // Only a notice speaks over the current step; maintenance status must not.
        if (boot_ != nullptr && ResolveNow().notice_active) {
            boot_->ShowBubble(text.c_str(), is_error);
        }
        return;
    }
    bubble_is_error_ = is_error;
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), static_cast<LvglTheme*>(current_theme_)->text_font()->font(),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(status_label_, AvatarBubbleLabelWidth(size.x, kAvatarBubbleMaxTextWidth));
    // A label of content height wraps long text instead of ending it in dots.
    lv_obj_set_height(status_label_, size.y);
    lv_label_set_text(status_label_, text.c_str());
    ApplyBubbleColorsLocked();
    lv_obj_remove_flag(status_bubble_, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t* step : tail_) {
        lv_obj_remove_flag(step, LV_OBJ_FLAG_HIDDEN);
    }
    AvatarPresentation presentation = ResolveNow();
    if (presentation.notice_active || presentation.maintenance) {
        operational_text_active_ = true;
    }
}

void AvatarStage::SetChatMessage(const char* role, const char* content) {
    // No conversation subtitles on any avatar; role decides nothing but the
    // operational path below, so no spoken text can leak.
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    AvatarPresentation presentation = ResolveNow();
    if (!ready_) {
        // A setup hint belongs on the boot screen. Anything else, such as the
        // user-agent line, does not show.
        if (boot_ != nullptr && presentation.notice_active && role != nullptr &&
            std::strcmp(role, "system") == 0 && content != nullptr) {
            boot_->SetNote(content);
        }
        return;
    }
    bool owner = presentation.notice_active || presentation.maintenance;
    if (owner && role != nullptr && std::strcmp(role, "system") == 0 && content != nullptr &&
        content[0] != '\0') {
        lv_label_set_text(notice_label_, content);
        notice_text_present_ = true;
        ReassertOperationalLocked();
        operational_text_active_ = true;
    }
}

void AvatarStage::ClearChatMessages() {
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    auto presentation = ResolveNow();
    if (!presentation.notice_active && !presentation.maintenance)
        ClearOperationalLocked();
}

void AvatarStage::SetHideSubtitle(bool hide) {
    // Intentionally a no-op in both directions: conversation text never shows.
    (void)hide;
}

void AvatarStage::EnsureNotificationTimer() {
    if (notification_expiry_timer_ == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = &AvatarStage::OnNotificationExpiry;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "avatar_notification_expiry";
        ESP_ERROR_CHECK(esp_timer_create(&args, &notification_expiry_timer_));
    }
}

void AvatarStage::OnNotificationExpiry(void* arg) {
    AvatarStage* self = static_cast<AvatarStage*>(arg);
    DisplayLockGuard lock(self);
    if (!lock) {
        return;
    }
    if (self->notification_label_ != nullptr) {
        lv_obj_add_flag(self->notification_label_, LV_OBJ_FLAG_HIDDEN);
        if (!self->notice_text_present_)
            lv_obj_add_flag(self->status_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    if (self->notice_text_present_) {
        lv_obj_remove_flag(self->notice_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AvatarStage::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (!setup_ui_called_ || notification == nullptr) {
        return;
    }
    if (!ready_) {
        // The bubble has the step, and a network's name must not appear here.
        return;
    }
    AvatarPresentation presentation = ResolveNow();
    if ((presentation.notice_active || presentation.maintenance) && notice_text_present_) {
        ESP_LOGI(TAG, "Operational notice keeps priority over notifications");
        return;
    }
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(notification_label_, notification);
    lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    if (notice_label_ != nullptr) {
        lv_obj_add_flag(notice_label_, LV_OBJ_FLAG_HIDDEN);
    }
    EnsureNotificationTimer();
    esp_timer_stop(notification_expiry_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_expiry_timer_, (int64_t)duration_ms * 1000));
}

void AvatarStage::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void AvatarStage::ClosePreviewWidgets() {
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

void AvatarStage::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "Preview ignored before the UI is set up");
        return;
    }
    if (image != nullptr) {
        if (!ready_) {
            ESP_LOGW(TAG, "Preview ignored during boot");
            return;
        }
        AvatarPresentation presentation = ResolveNow();
        if ((presentation.notice_active || presentation.maintenance) && operational_text_active_) {
            ESP_LOGI(TAG, "Preview blocked by an active operational notice");
            return;
        }
        // Base behavior: cache the image, hide the avatar's layer, show the
        // preview and arm the 5 s preview timer.
        LcdDisplay::SetPreviewImage(std::move(image));
        preview_open_ = true;
        return;
    }
    // Close path (explicit or from the registered base preview timer): close the
    // widgets, then apply the current presentation, never a cached one.
    ClosePreviewWidgets();
    if (!destructing_) {
        ApplyPresentation(ResolveNow());
    }
}

void AvatarStage::UpdateStatusBar(bool update_all) {
    DisplayLockGuard state_lock(this);
    if (!state_lock)
        return;
    // The base may format a clock string and route it through the virtual
    // SetStatus; mark it routine so SetStatus rejects it. Indicators and the
    // low-battery popup keep working.
    routine_status_update_ = true;
    LvglDisplay::UpdateStatusBar(update_all);
    routine_status_update_ = false;
    if (clock_label_ == nullptr) {
        return;
    }
    char text[8];
    if (!FormatAvatarClock(time(nullptr), text, sizeof(text))) {
        lv_obj_add_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // Only a new minute redraws the corner of the avatar it sits on.
    if (std::strcmp(lv_label_get_text(clock_label_), text) != 0) {
        lv_label_set_text(clock_label_, text);
    }
    lv_obj_remove_flag(clock_label_, LV_OBJ_FLAG_HIDDEN);
}
