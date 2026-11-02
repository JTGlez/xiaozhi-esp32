#include "conversation_visual_session.h"
#include <utility>

void ConversationVisualSession::Configure(bool enabled, Sink sink) {
    enabled_ = enabled;
    sink_ = std::move(sink);
}

uint32_t ConversationVisualSession::BeginTurn(uint64_t now_ms) {
    const auto generation = generation_.fetch_add(1) + 1;
    Dispatch(MakeEvent(ConversationVisualEventKind::TurnStarted, now_ms));
    return generation;
}

ConversationVisualEvent ConversationVisualSession::MakeEvent(ConversationVisualEventKind kind,
                                                             uint64_t now_ms) const {
    ConversationVisualEvent event{};
    event.kind = kind;
    event.generation = generation();
    event.activity = activity_;
    event.now_ms = now_ms;
    return event;
}

bool ConversationVisualSession::Accepts(uint32_t generation) const {
    return generation == this->generation();
}

void ConversationVisualSession::Dispatch(const ConversationVisualEvent& event) {
    if (enabled_ && sink_ && Accepts(event.generation))
        sink_(event);
}

void ConversationVisualSession::SetActivity(ConversationActivity activity, uint64_t now_ms) {
    activity_ = activity;
    Dispatch(MakeEvent(ConversationVisualEventKind::Activity, now_ms));
}

bool ConversationVisualSession::FinalTranscript(std::string_view transcript, uint64_t now_ms) {
    if (transcript.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos)
        return false;
    Dispatch(MakeEvent(ConversationVisualEventKind::FinalTranscript, now_ms));
    return true;
}

void ConversationVisualSession::Cancel(uint64_t now_ms) {
    BeginTurn(now_ms);  // invalidate queued local callbacks before cancellation
    Dispatch(MakeEvent(ConversationVisualEventKind::Cancelled, now_ms));
}

void ConversationVisualSession::NoteAudioActivity(uint32_t generation, uint64_t now_ms) {
    audio_signal_.store((static_cast<uint64_t>(generation) << 32) | static_cast<uint32_t>(now_ms));
}

bool ConversationVisualSession::ConsumeAudioActivity(uint64_t now_ms) {
    const uint64_t signal = audio_signal_.load();
    if (signal == consumed_signal_)
        return false;
    consumed_signal_ = signal;
    if (!Accepts(static_cast<uint32_t>(signal >> 32)))
        return false;
    // Reconstruct the most recent timestamp across the 32-bit millisecond wrap.
    const uint32_t elapsed = static_cast<uint32_t>(now_ms) - static_cast<uint32_t>(signal);
    auto event = MakeEvent(ConversationVisualEventKind::AudioActivity, now_ms - elapsed);
    Dispatch(event);
    return enabled_;
}
