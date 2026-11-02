#ifndef CONVERSATION_VISUAL_H
#define CONVERSATION_VISUAL_H

#include <cstdint>
#include <string_view>

// Semantic conversation states published by the application layer.
// These are not DeviceState values and never replace the audio/connection
// state machine.
enum class ConversationActivity {
    Idle,
    Connecting,
    Listening,
    Maintenance,
};

// Optional display events. A display that does not opt in ignores all of them.
enum class ConversationVisualEventKind {
    Activity,
    TurnStarted,
    FinalTranscript,
    Emotion,
    TtsStarted,
    TtsStopped,
    PlaybackState,
    AudioActivity,
    NotificationStarted,
    NotificationFinished,
    Cancelled,
    NoticeStarted,
    NoticeCleared,
    Tick,
};

// Spoken wording never crosses this contract: captions, replies and alerts keep
// flowing through the existing wording paths. `emotion` is borrowed for the
// duration of the call only; owners must capture their own copy before
// crossing Schedule().
struct ConversationVisualEvent {
    ConversationVisualEventKind kind;
    uint32_t generation = 0;
    uint32_t playback_id = 0;
    uint64_t now_ms = 0;
    ConversationActivity activity = ConversationActivity::Idle;
    bool playback_idle = true;
    std::string_view emotion;
};

#endif  // CONVERSATION_VISUAL_H
