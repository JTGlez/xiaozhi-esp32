#ifndef ELENA_AVATAR_DISPLAY_H
#define ELENA_AVATAR_DISPLAY_H

#include "avatar_controller.h"
#include "lcd_display.h"

#include <esp_timer.h>
#include <lvgl.h>

// Elena's avatar view: a SpiLcdDisplay specialization that keeps the GIF
// engine, draws the full 320x240 canvas without scaling, adds a transparent
// top indicator strip, never shows conversation text or the bottom bar, and
// drives expressions from the semantic AvatarController instead of from
// transcription wording. Routine base updates (clock, idle "neutral", chat
// subtitles) are rejected here; only operational notices owned by a semantic
// NoticeStarted/Maintenance activity can put text on screen.
class ElenaAvatarDisplay : public SpiLcdDisplay {
public:
    // Cream kit background kept constant across themes (spec section 4).
    static constexpr uint32_t kElenaCanvasCream = 0xF9F0E9;

    ElenaAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                       bool swap_xy);
    ~ElenaAvatarDisplay() override;

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetHideSubtitle(bool hide) override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetTheme(Theme* theme) override;
    void UpdateStatusBar(bool update_all = false) override;
    bool SupportsConversationVisuals() const override { return true; }
    void OnConversationVisualEvent(const ConversationVisualEvent& event) override;

private:
    // Applies a resolved presentation: operational text layer first, then
    // the avatar expression with the same-expression fast path, a single
    // neutral retry and memoized failed attempts.
    void ApplyPresentation(const AvatarPresentation& presentation);
    // Static fallback from the generated header: a borrowed descriptor, no
    // frame allocation or copy.
    void ShowStaticFallback();

    // Returns the collection entry only if it is a usable GIF resource.
    const LvglImage* GetValidGif(const char* emotion) const;
    bool TryExpression(const char* emotion);
    void ClearExpressionLocked();
    void ShowStaticFallbackLocked();
    void ReassertOperationalLocked();
    void ClearOperationalLocked();
    void ClosePreviewWidgets();
    void ApplyElenaLayerLocked();
    void EnsureNotificationTimer();
    static void OnNotificationExpiry(void* arg);
    static uint64_t NowMs();

    AvatarController controller_;
    lv_img_dsc_t elena_fallback_dsc_{};

    bool preview_open_ = false;
    bool routine_status_update_ = false;
    bool operational_text_active_ = false;

    // Applied-presentation bookkeeping. Identity is the exact lv_img_dsc_t
    // pointer on screen plus the EmojiCollection instance, never the emotion
    // string or Theme pointer alone: an asset reload may reuse the same
    // Theme with a different collection.
    AvatarPresentation applied_presentation_;
    bool applied_valid_ = false;
    bool applied_via_gif_ = false;
    const EmojiCollection* applied_collection_ = nullptr;
    const lv_img_dsc_t* applied_identity_ = nullptr;

    // One-shot failure memo: a resolved expression already known to be
    // unshowable in the current collection goes straight to the static
    // fallback instead of reopening decoders on every tick.
    const char* failed_emotion_ = nullptr;
    const EmojiCollection* failed_collection_ = nullptr;

    esp_timer_handle_t notification_expiry_timer_ = nullptr;
    bool destructing_ = false;
};

#endif  // ELENA_AVATAR_DISPLAY_H
