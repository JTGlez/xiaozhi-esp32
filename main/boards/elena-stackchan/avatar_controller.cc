#include "avatar_controller.h"

#include <algorithm>
#include <cstddef>

namespace {

constexpr const char* kNeutral = "neutral";
constexpr const char* kListening = "listening";
constexpr const char* kThinking = "thinking";
constexpr const char* kSpeaking = "speaking";

// Fixed alias table: the 12 Elena GIF names resolve to themselves and every
// received alias points at one of those same files. No path is ever built from
// a received name.
constexpr AvatarEmotionAlias kAvatarEmotionAliases[] = {
    {"neutral", kNeutral},
    {"listening", kListening},
    {"thinking", kThinking},
    {"speaking", kSpeaking},
    {"happy", "happy"},
    {"sad", "sad"},
    {"sleeping", "sleeping"},
    {"pet", "pet"},
    {"shake", "shake"},
    {"look_left", "look_left"},
    {"look_right", "look_right"},
    {"email", "email"},

    {"loving", "pet"},
    {"love", "pet"},
    {"kissy", "pet"},
    {"embarrassed", "pet"},
    {"crying", "sad"},
    {"sleepy", "sleeping"},
    {"doubtful", "shake"},
    {"dizzy", "shake"},
    {"confused", "shake"},
    {"surprised", "shake"},
    {"shocked", "shake"},
    {"laughing", "happy"},
    {"funny", "happy"},
    {"silly", "happy"},
    {"cool", "happy"},
    {"delicious", "happy"},
    {"confident", "happy"},
    {"winking", "look_left"},
    {"relaxed", kNeutral},
    {"angry", kNeutral},
    {"scowl", kNeutral},
    {"talking", kSpeaking},
    {"mail", "email"},
    {"sending", "email"},
};

char LowerAscii(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

}  // namespace

bool IsValidAvatarEmotionName(std::string_view name) {
    if (name.empty() || name.size() > kAvatarEmotionNameMaxLen) {
        return false;
    }
    for (const char value : name) {
        const bool accepted = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                              (value >= '0' && value <= '9') || value == '_' || value == '-';
        if (!accepted) {
            return false;
        }
    }
    return true;
}

const char* NormalizeAvatarEmotion(std::string_view name) {
    if (!IsValidAvatarEmotionName(name)) {
        return kNeutral;
    }

    char lowered[kAvatarEmotionNameMaxLen + 1];
    std::size_t index = 0;
    for (; index < name.size(); ++index) {
        lowered[index] = LowerAscii(name[index]);
    }
    lowered[index] = '\0';
    const std::string_view key(lowered, index);

    for (const AvatarEmotionAlias& alias : kAvatarEmotionAliases) {
        if (key == std::string_view(alias.from)) {
            return alias.to;
        }
    }
    return kNeutral;
}

std::size_t AvatarEmotionAliasCount() {
    return sizeof(kAvatarEmotionAliases) / sizeof(kAvatarEmotionAliases[0]);
}

const AvatarEmotionAlias& AvatarEmotionAliasAt(std::size_t index) {
    return kAvatarEmotionAliases[index];
}

bool AvatarController::IsMaintenance(uint64_t now_ms) const {
    (void)now_ms;
    return activity_ == ConversationActivity::Maintenance;
}

bool AvatarController::IsThinking(uint64_t now_ms) const {
    if (IsMaintenance(now_ms)) {
        return false;
    }
    return thinking_active_ && now_ms < thinking_deadline_ms_;
}

bool AvatarController::IsSpeaking(uint64_t now_ms) const {
    if (IsMaintenance(now_ms)) {
        return false;
    }
    if (notification_active_) {
        // A spoken notification follows its own cycle and playback identifier.
        // Its drain and its end may arrive in either order; both must be seen.
        if (notification_finished_ && notification_drained_) {
            return false;
        }
        // Recovery only: 30 s without an accepted audio signal closes the
        // visual cycle. It never stops the notification itself.
        return !playback_idle_ || now_ms < last_audio_ms_ + kAvatarAudioIdleTimeoutMs;
    }
    if (!tts_cycle_active_) {
        return false;
    }
    // Pending playback always owns the view. Recovery requires BOTH idle
    // playback and 30 s without accepted audio; a timer never hides queued audio.
    return !playback_idle_ || now_ms < last_audio_ms_ + kAvatarAudioIdleTimeoutMs;
}

uint64_t AvatarController::CycleEndMs(uint64_t now_ms) const {
    (void)now_ms;
    uint64_t end_ms = 0;
    if (thinking_active_) {
        end_ms = std::max(end_ms, thinking_deadline_ms_);
    }
    if (tts_cycle_active_ || notification_active_) {
        // Without a drain this cycle ends when the recovery window expires.
        end_ms = std::max(end_ms, last_audio_ms_ + kAvatarAudioIdleTimeoutMs);
    }
    return end_ms;
}

bool AvatarController::IsMoodActive(uint64_t now_ms) const {
    if (!mood_active_) {
        return false;
    }
    if (now_ms < mood_deadline_ms_) {
        return true;
    }
    // The rest window pauses while a thinking wait or a speaking cycle owns the
    // avatar, so it always runs 5 s from the moment that cycle ends.
    const uint64_t cycle_end_ms = CycleEndMs(now_ms);
    return cycle_end_ms != 0 && now_ms < cycle_end_ms + kAvatarMoodRestMs;
}

void AvatarController::RearmMood(uint64_t settled_ms) {
    if (mood_active_) {
        mood_deadline_ms_ = settled_ms + kAvatarMoodRestMs;
    }
}

void AvatarController::EndSpeakingCycle(uint64_t now_ms, const char* reason) {
    // Event-driven completion uses the actual drain/stop time. Recovery
    // callers explicitly pass the expired deadline instead.
    tts_cycle_active_ = false;
    tts_stop_pending_ = false;
    notification_active_ = false;
    notification_finished_ = false;
    notification_drained_ = false;
    thinking_active_ = false;
    // The conversation mood stays available for the rest window.
    RearmMood(now_ms);
    expiry_reason_ = reason;
}

void AvatarController::CancelTurn(const char* reason) {
    tts_cycle_active_ = false;
    tts_stop_pending_ = false;
    notification_active_ = false;
    notification_finished_ = false;
    notification_drained_ = false;
    thinking_active_ = false;
    mood_active_ = false;
    expiry_reason_ = reason;
}

void AvatarController::Sweep(uint64_t now_ms) {
    if (notification_active_ && !IsSpeaking(now_ms)) {
        EndSpeakingCycle(last_audio_ms_ + kAvatarAudioIdleTimeoutMs, "notification_timeout");
    }
    if (tts_cycle_active_ && !IsSpeaking(now_ms)) {
        EndSpeakingCycle(last_audio_ms_ + kAvatarAudioIdleTimeoutMs, "audio_idle_timeout");
    }
    if (thinking_active_ && !IsThinking(now_ms)) {
        const uint64_t settled_ms = std::min(now_ms, thinking_deadline_ms_);
        thinking_active_ = false;
        expiry_reason_ = "wait_timeout";
        RearmMood(settled_ms);
    }
    if (mood_active_ && !IsMoodActive(now_ms)) {
        mood_active_ = false;
    }
}

void AvatarController::Handle(const ConversationVisualEvent& event) {
    const uint64_t now_ms = event.now_ms;
    const bool turn_scoped = event.kind == ConversationVisualEventKind::FinalTranscript ||
                             event.kind == ConversationVisualEventKind::Emotion ||
                             event.kind == ConversationVisualEventKind::TtsStarted ||
                             event.kind == ConversationVisualEventKind::TtsStopped ||
                             event.kind == ConversationVisualEventKind::PlaybackState ||
                             event.kind == ConversationVisualEventKind::AudioActivity ||
                             event.kind == ConversationVisualEventKind::NotificationStarted ||
                             event.kind == ConversationVisualEventKind::NotificationFinished ||
                             event.kind == ConversationVisualEventKind::Cancelled;

    switch (event.kind) {
        case ConversationVisualEventKind::TurnStarted:
            // A local turn identifier only moves forward; older turns cannot
            // replace the current one.
            if (event.generation > generation_) {
                generation_ = event.generation;
                activity_ = event.activity;
                CancelTurn("turn_replaced");
            }
            break;

        case ConversationVisualEventKind::Activity:
            activity_ = event.activity;
            if (activity_ == ConversationActivity::Maintenance) {
                // The notice owns the screen until its owner releases it.
                CancelTurn("maintenance");
            }
            break;

        case ConversationVisualEventKind::NoticeStarted:
            notice_active_ = true;
            break;

        case ConversationVisualEventKind::NoticeCleared:
            notice_active_ = false;
            break;

        case ConversationVisualEventKind::Tick:
            break;

        default:
            if (!turn_scoped || event.generation != generation_) {
                // Callback of an earlier turn, or an event that does not belong
                // to the current cycle: discarded.
                Sweep(now_ms);
                return;
            }
            break;
    }

    switch (event.kind) {
        case ConversationVisualEventKind::TurnStarted:
        case ConversationVisualEventKind::Activity:
        case ConversationVisualEventKind::NoticeStarted:
        case ConversationVisualEventKind::NoticeCleared:
        case ConversationVisualEventKind::Tick:
            break;

        case ConversationVisualEventKind::FinalTranscript:
            if (!IsMaintenance(now_ms) && !thinking_active_ && !tts_cycle_active_) {
                thinking_active_ = true;
                thinking_deadline_ms_ = now_ms + kAvatarWaitTimeoutMs;
            }
            break;

        case ConversationVisualEventKind::Emotion:
            if (!IsMaintenance(now_ms)) {
                mood_ = NormalizeAvatarEmotion(event.emotion);
                mood_active_ = true;
                mood_deadline_ms_ = now_ms + kAvatarMoodRestMs;
            }
            break;

        case ConversationVisualEventKind::TtsStarted:
            if (!IsMaintenance(now_ms) && (!tts_cycle_active_ || tts_stop_pending_)) {
                playback_idle_ = event.playback_idle;
                // A valid answer also ends the visual wait.
                thinking_active_ = false;
                tts_cycle_active_ = true;
                // Another reply in the same turn: the earlier stop no longer
                // describes the audio in the queue.
                tts_stop_pending_ = false;
                last_audio_ms_ = now_ms;
            }
            break;

        case ConversationVisualEventKind::TtsStopped:
            if (!tts_cycle_active_) {
                break;
            }
            if (event.playback_idle) {
                EndSpeakingCycle(now_ms, "tts_stopped");
            } else {
                // The stop itself says the player still holds audio, even when
                // no PlaybackState(false) was seen before it.
                tts_stop_pending_ = true;
                playback_idle_ = false;
                last_audio_ms_ = now_ms;
            }
            break;

        case ConversationVisualEventKind::PlaybackState:
            playback_idle_ = event.playback_idle;
            if (!playback_idle_) {
                break;
            }
            if (notification_active_) {
                if (event.playback_id == notification_playback_id_) {
                    notification_drained_ = true;
                    if (notification_finished_) {
                        EndSpeakingCycle(now_ms, "notification_finished");
                    }
                }
                break;
            }
            if (tts_cycle_active_ && tts_stop_pending_) {
                EndSpeakingCycle(now_ms, "playback_drained");
            }
            break;

        case ConversationVisualEventKind::AudioActivity:
            // Accepted audio of the current cycle resets the recovery window.
            last_audio_ms_ = now_ms;
            break;

        case ConversationVisualEventKind::NotificationStarted:
            playback_idle_ = event.playback_idle;
            notification_active_ = true;
            notification_playback_id_ = event.playback_id;
            notification_finished_ = false;
            notification_drained_ = false;
            last_audio_ms_ = now_ms;
            break;

        case ConversationVisualEventKind::NotificationFinished:
            if (!notification_active_ || event.playback_id != notification_playback_id_) {
                break;
            }
            notification_finished_ = true;
            last_audio_ms_ = now_ms;
            // The drain may already have happened: notify_player.cc leaves the
            // player idle before it announces the end of the notification.
            if (notification_drained_) {
                EndSpeakingCycle(now_ms, "notification_finished");
            }
            break;

        case ConversationVisualEventKind::Cancelled:
            CancelTurn("cancelled");
            break;
    }

    Sweep(now_ms);
}

AvatarPresentation AvatarController::Resolve(uint64_t now_ms) const {
    AvatarPresentation presentation;
    presentation.notice_active = notice_active_;
    presentation.maintenance = IsMaintenance(now_ms);

    if (presentation.maintenance) {
        presentation.emotion = kNeutral;
    } else if (IsSpeaking(now_ms)) {
        presentation.emotion = kSpeaking;
    } else if (IsThinking(now_ms)) {
        presentation.emotion = kThinking;
    } else if (activity_ == ConversationActivity::Listening) {
        presentation.emotion = kListening;
    } else if (IsMoodActive(now_ms)) {
        presentation.emotion = mood_;
    } else {
        presentation.emotion = kNeutral;
    }
    return presentation;
}
