#include "conversation_visual_session.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>
#include <vector>
#include "avatar_controller.h"
using K = ConversationVisualEventKind;
using A = ConversationActivity;
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string_view name(argv[1]);
    AvatarController controller;
    std::vector<ConversationVisualEvent> events;
    ConversationVisualSession s;
    s.Configure(true, [&](const auto& e) {
        events.push_back(e);
        controller.Handle(e);
    });
    auto expect = [&](uint64_t t, const char* mood) {
        assert(std::string_view(controller.Resolve(t).emotion) == mood);
    };
    auto emit = [&](K kind, uint64_t time, bool idle = true, uint32_t id = 0) {
        auto e = s.MakeEvent(kind, time);
        e.playback_idle = idle;
        e.playback_id = id;
        s.Dispatch(e);
    };
    s.SetActivity(A::Listening, 0);
    s.BeginTurn(0);
    if (name == "stop_state_change_and_drain_permutations") {
        std::array<int, 3> order{0, 1, 2};
        do {
            s.BeginTurn(100);
            emit(K::TtsStarted, 101, false);
            for (int i : order) {
                if (i == 0)
                    emit(K::TtsStopped, 102,
                         std::find(order.begin(), std::find(order.begin(), order.end(), 0), 1) !=
                             std::find(order.begin(), order.end(), 0));
                if (i == 1)
                    emit(K::PlaybackState, 103, true);
                if (i == 2)
                    s.SetActivity(A::Listening, 104);
            }
            expect(105, "listening");
        } while (std::next_permutation(order.begin(), order.end()));
        s.BeginTurn(200);
        emit(K::TtsStarted, 201, false);
        emit(K::PlaybackState, 202, true);
        expect(203, "speaking");
        emit(K::TtsStopped, 204, true);
        expect(204, "listening");
    } else if (name == "realtime_capture_without_state_transition") {
        s.FinalTranscript("hello", 1);
        expect(1, "thinking");
        auto old = s.MakeEvent(K::TtsStarted, 2);
        s.BeginTurn(3);
        s.Dispatch(old);
        expect(3, "listening");
        s.FinalTranscript("second", 4);
        expect(4, "thinking");
    } else if (name == "local_callbacks_are_generation_guarded") {
        auto old = s.MakeEvent(K::Emotion, 1);
        old.emotion = "happy";
        auto generation = old.generation;
        s.BeginTurn(2);
        assert(!s.Accepts(generation));
        s.Dispatch(old);
        s.SetActivity(A::Idle, 3);
        expect(3, "neutral");
        // No server turn identity exists: a newly received stop is current.
        emit(K::TtsStarted, 4, false);
        emit(K::TtsStopped, 5, true);
        expect(5, "neutral");
    } else if (name == "notification_finish_and_cancel") {
        emit(K::NotificationStarted, 1, false, 7);
        emit(K::NotificationFinished, 2, true, 8);
        expect(2, "speaking");
        emit(K::PlaybackState, 3, true, 7);
        expect(3, "speaking");
        emit(K::NotificationFinished, 4, true, 7);
        expect(4, "listening");
        emit(K::NotificationStarted, 5, false, 9);
        s.Cancel(6);
        expect(6, "listening");
    } else if (name == "empty_stt_and_vad_silence_do_not_think") {
        for (auto text : {"", " ", "\t\r\n"})
            assert(!s.FinalTranscript(text, 1));
        expect(1, "listening");
        emit(K::Tick, 2);
        expect(2, "listening");
        assert(s.FinalTranscript("日本語", 3));
        expect(3, "thinking");
    } else if (name == "operational_notice_lifecycle") {
        emit(K::NoticeStarted, 1);
        auto e = s.MakeEvent(K::Emotion, 2);
        e.emotion = "happy";
        s.Dispatch(e);
        assert(controller.Resolve(2).notice_active);
        s.Cancel(3);
        assert(controller.Resolve(3).notice_active);
        emit(K::NoticeCleared, 4);
        assert(!controller.Resolve(4).notice_active);
        s.SetActivity(A::Maintenance, 5);
        expect(5, "neutral");
    } else if (name == "no_events_for_unsupported_display") {
        events.clear();
        s.Configure(false, [&](const auto& e) { events.push_back(e); });
        s.BeginTurn(1);
        s.FinalTranscript("hello", 2);
        s.SetActivity(A::Listening, 3);
        s.NoteAudioActivity(s.generation(), 4);
        s.ConsumeAudioActivity(5);
        emit(K::Tick, 6);
        assert(events.empty());
    } else if (name == "audio_events_are_coalesced") {
        s.SetActivity(A::Idle, 1);
        emit(K::TtsStarted, 2, true);
        events.clear();
        for (uint32_t t = 3; t < 20003; ++t)
            s.NoteAudioActivity(s.generation(), t);
        assert(events.empty());
        assert(s.ConsumeAudioActivity(20003));
        assert(events.size() == 1 && events[0].kind == K::AudioActivity);
        assert(events[0].now_ms == 20002);
        assert(!s.ConsumeAudioActivity(20004));
        expect(50001, "speaking");
        expect(50002, "neutral");
        auto old = s.generation();
        s.BeginTurn(60000);
        s.NoteAudioActivity(old, 60001);
        assert(!s.ConsumeAudioActivity(60002));
    } else
        return 2;
}
