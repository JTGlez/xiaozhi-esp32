#ifndef AVATAR_STAGE_H
#define AVATAR_STAGE_H

#include "avatar_controller.h"
#include "avatar_renderer.h"
#include "boot_step.h"
#include "lcd_display.h"

#include <esp_timer.h>
#include <lvgl.h>

#include <array>
#include <memory>
#include <mutex>

class BootScreen;

inline constexpr uint32_t kAvatarErrorColor = 0xC62828;
inline constexpr int kAvatarBubbleMaxTextWidth = 176;

// The UI that every avatar of this board shares: a transparent top bar with a
// status bubble, clock and icons, and a strip for operational notices. The avatar
// itself is an AvatarRenderer drawing in a layer below all of that, so a new
// avatar changes nothing here. Conversation text never reaches the screen.
//
// A boot screen covers all of that until Application reports kReady: only then
// does the renderer exist, so the avatar does not load without its server.
class AvatarStage : public SpiLcdDisplay, private AvatarArtSource {
public:
    AvatarStage(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    ~AvatarStage() override;

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
    void SetPowerSaveMode(bool on) override;
    void OnBootProgress(BootStep step, int retry_seconds) override;
    bool RequiresServerAtBoot() const override { return true; }

protected:
    AvatarRenderer* renderer() const { return renderer_.get(); }
    BootScreen* boot_screen() const { return boot_.get(); }

    lv_obj_t* status_bubble_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    std::array<lv_obj_t*, 4> tail_{};

private:
    std::shared_ptr<EmojiCollection> Collection() const override;

    void CreateLayoutLocked();
    void ApplyPresentation(const AvatarPresentation& presentation);
    void ApplyStageStyleLocked();
    void ApplyBubbleColorsLocked();
    void ReassertOperationalLocked();
    void ClearOperationalLocked();
    void ClosePreviewWidgets();
    void EnsureNotificationTimer();
    static void OnNotificationExpiry(void* arg);
    AvatarPresentation ResolveNow();
    static uint64_t NowMs();

    // Short critical sections only, and never held while waiting for the LVGL
    // lock: the conversation tick must not block on a render.
    std::mutex controller_mutex_;
    AvatarController controller_;
    AvatarPresentation last_requested_;
    bool last_requested_valid_ = false;

    std::unique_ptr<BootScreen> boot_;
    bool ready_ = false;
    // An error from boot, shown once the bubble exists. A later status clears it.
    bool boot_error_pending_ = false;
    std::unique_ptr<AvatarRenderer> renderer_;
    // What the renderer last received. Present runs only when this changes.
    AvatarPresentation presented_;
    bool presented_valid_ = false;
    const EmojiCollection* presented_collection_ = nullptr;

    lv_obj_t* right_group_ = nullptr;
    // Second strip, below the top bar, for operational notices.
    lv_obj_t* notice_label_ = nullptr;
    bool notice_text_present_ = false;

    bool bubble_is_error_ = false;
    bool preview_open_ = false;
    bool routine_status_update_ = false;
    bool operational_text_active_ = false;
    bool power_save_ = false;

    esp_timer_handle_t notification_expiry_timer_ = nullptr;
    bool destructing_ = false;
};

#endif  // AVATAR_STAGE_H
