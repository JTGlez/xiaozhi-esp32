#ifndef AVATAR_CONTROLLER_H
#define AVATAR_CONTROLLER_H

#include <cstdint>
#include <string_view>

#include "conversation_visual.h"

// Visual recovery windows. They never cancel a network request, audio playback
// or queue drain. The mood window pauses while a thinking wait or a speaking
// cycle owns the avatar, so it always lasts 5 s after the last cycle ends.
inline constexpr uint64_t kAvatarMoodRestMs = 5000;
inline constexpr uint64_t kAvatarWaitTimeoutMs = 30000;
inline constexpr uint64_t kAvatarAudioIdleTimeoutMs = 30000;
inline constexpr std::size_t kAvatarEmotionNameMaxLen = 40;

// Accepted input: 1..40 ASCII characters of [A-Za-z0-9_-].
bool IsValidAvatarEmotionName(std::string_view name);

// Resolves a received name against the fixed alias table. Returns a static
// string; unknown, empty, invalid or over-long input resolves to "neutral".
const char* NormalizeAvatarEmotion(std::string_view name);

// Resolved presentation. Every `emotion` points to static storage.
struct AvatarPresentation {
    const char* emotion = "neutral";
    bool notice_active = false;
    bool maintenance = false;

    bool operator==(const AvatarPresentation& other) const {
        return std::string_view(emotion) == std::string_view(other.emotion) &&
               notice_active == other.notice_active && maintenance == other.maintenance;
    }
};

// Resolves conversation events into one presentation. The clock is supplied as
// data, so the whole class is testable on the host without LVGL or ESP-IDF.
class AvatarController {
public:
    void Handle(const ConversationVisualEvent& event);
    AvatarPresentation Resolve(uint64_t now_ms) const;

    // Why the last handled event ended a visual cycle: "" when nothing ended.
    const char* ExpiryReason() const { return expiry_reason_; }

    uint32_t generation() const { return generation_; }
    ConversationActivity activity() const { return activity_; }

private:
    bool IsMaintenance(uint64_t now_ms) const;
    bool IsSpeaking(uint64_t now_ms) const;
    bool IsThinking(uint64_t now_ms) const;
    bool IsMoodActive(uint64_t now_ms) const;
    uint64_t CycleEndMs(uint64_t now_ms) const;
    void RearmMood(uint64_t settled_ms);
    void EndSpeakingCycle(uint64_t now_ms, const char* reason);
    void CancelTurn(const char* reason);
    void Sweep(uint64_t now_ms);

    uint32_t generation_ = 0;
    ConversationActivity activity_ = ConversationActivity::Idle;
    bool notice_active_ = false;

    bool thinking_active_ = false;
    uint64_t thinking_deadline_ms_ = 0;

    bool tts_cycle_active_ = false;
    bool tts_stop_pending_ = false;
    uint64_t last_audio_ms_ = 0;
    bool playback_idle_ = true;

    bool notification_active_ = false;
    uint32_t notification_playback_id_ = 0;
    bool notification_finished_ = false;
    bool notification_drained_ = false;

    const char* mood_ = "neutral";
    bool mood_active_ = false;
    uint64_t mood_deadline_ms_ = 0;

    const char* expiry_reason_ = "";
};

#endif  // AVATAR_CONTROLLER_H
