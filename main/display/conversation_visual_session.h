#ifndef CONVERSATION_VISUAL_SESSION_H
#define CONVERSATION_VISUAL_SESSION_H

#include <atomic>
#include <functional>
#include "conversation_visual.h"

// Application-side adapter. All methods except generation()/NoteAudioActivity()
// run on the main task. The audio signal is a single atomic mailbox, never a
// scheduled callback or allocation for each received packet.
class ConversationVisualSession {
public:
    using Sink = std::function<void(const ConversationVisualEvent&)>;
    void Configure(bool enabled, Sink sink);
    uint32_t BeginTurn(uint64_t now_ms);
    ConversationVisualEvent MakeEvent(ConversationVisualEventKind kind, uint64_t now_ms) const;
    bool Accepts(uint32_t generation) const;
    uint32_t generation() const { return generation_.load(); }
    void Dispatch(const ConversationVisualEvent& event);
    void SetActivity(ConversationActivity activity, uint64_t now_ms);
    bool FinalTranscript(std::string_view transcript, uint64_t now_ms);
    void Cancel(uint64_t now_ms);
    void NoteAudioActivity(uint32_t generation, uint64_t now_ms);
    bool ConsumeAudioActivity(uint64_t now_ms);

private:
    bool enabled_ = false;
    Sink sink_;
    ConversationActivity activity_ = ConversationActivity::Idle;
    std::atomic<uint32_t> generation_{0};
    std::atomic<uint64_t> audio_signal_{0};
    uint64_t consumed_signal_ = 0;
};
#endif
