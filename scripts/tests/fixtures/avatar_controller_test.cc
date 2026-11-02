// Host harness for the conversation avatar contract and AvatarController.
// Compiled by scripts/tests/test_avatar_controller.py with the host compiler.
// Each case is selected by argv[1] so one Python test asserts one behaviour.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

#include "avatar_controller.h"
#include "conversation_visual.h"

using Activity = ConversationActivity;
using Kind = ConversationVisualEventKind;

namespace {

ConversationVisualEvent Ev(Kind kind, uint32_t generation, uint64_t now_ms,
                           uint32_t playback_id = 0, bool playback_idle = true,
                           Activity activity = Activity::Idle, std::string_view emotion = {}) {
    ConversationVisualEvent event;
    event.kind = kind;
    event.generation = generation;
    event.playback_id = playback_id;
    event.now_ms = now_ms;
    event.activity = activity;
    event.playback_idle = playback_idle;
    event.emotion = emotion;
    return event;
}

void Expect(const char* case_name, const AvatarPresentation& got, const char* emotion,
            bool notice_active, bool maintenance) {
    const AvatarPresentation want{emotion, notice_active, maintenance};
    if (!(got == want)) {
        std::fprintf(stderr,
                     "%s: got emotion=%s notice_active=%d maintenance=%d; "
                     "want emotion=%s notice_active=%d maintenance=%d\n",
                     case_name, got.emotion ? got.emotion : "(null)",
                     static_cast<int>(got.notice_active), static_cast<int>(got.maintenance),
                     emotion, static_cast<int>(notice_active), static_cast<int>(maintenance));
        std::exit(1);
    }
}

void ExpectEmotion(const char* case_name, std::string_view name, std::string_view result,
                   std::string_view want) {
    if (result != want) {
        const std::string input(name), got(result), expected(want);
        std::fprintf(stderr, "%s: NormalizeAvatarEmotion(\"%s\") = \"%s\"; want \"%s\"\n",
                     case_name, input.c_str(), got.c_str(), expected.c_str());
        std::exit(1);
    }
}

void ExpectAlias(const char* name, std::string_view want) {
    ExpectEmotion("test_all_aliases_and_invalid_names", name, NormalizeAvatarEmotion(name), want);
}

// Alikes must be observable through the resolved presentation too.
void ExpectResolvedEmotion(const char* case_name, const AvatarController& controller,
                           uint64_t now_ms, const char* emotion) {
    Expect(case_name, controller.Resolve(now_ms), emotion, false, false);
}

void PlanAudioSnippet() {
    // Aserciones del plan, literalmente: stop con audio pendiente mantiene
    // speaking; el ciclo termina solo cuando el audio está drenado.
    AvatarController c;
    ConversationVisualEvent e{Kind::TurnStarted, 1, 0, 0};
    c.Handle(e);
    e.kind = Kind::TtsStarted;
    c.Handle(e);
    e.kind = Kind::PlaybackState;
    e.playback_idle = false;
    c.Handle(e);
    e.kind = Kind::TtsStopped;
    c.Handle(e);
    assert(std::string_view(c.Resolve(0).emotion) == "speaking");
    e.kind = Kind::PlaybackState;
    e.playback_idle = true;
    c.Handle(e);
    assert(std::string_view(c.Resolve(0).emotion) != "speaking");
}

void TestStopRequiresDrained() {
    PlanAudioSnippet();

    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false));
    c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
    c.Handle(Ev(Kind::PlaybackState, 1, 200, 0, false));
    c.Handle(Ev(Kind::TtsStopped, 1, 300, 0, false));
    Expect("test_stop_requires_drained", c.Resolve(300), "speaking", false, false);
    c.Handle(Ev(Kind::PlaybackState, 1, 400, 0, true));
    Expect("test_stop_requires_drained", c.Resolve(400), "neutral", false, false);

    // Orden inverso: un drenado momentáneo entre fragmentos no cierra el ciclo.
    AvatarController d;
    d.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false));
    d.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
    d.Handle(Ev(Kind::PlaybackState, 1, 200, 0, false));
    d.Handle(Ev(Kind::PlaybackState, 1, 300, 0, true));
    Expect("test_stop_requires_drained", d.Resolve(300), "speaking", false, false);
    d.Handle(Ev(Kind::TtsStopped, 1, 400, 0, true));
    Expect("test_stop_requires_drained", d.Resolve(400), "neutral", false, false);
}

void TestNewTurnRejectsStaleCallbacks() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::FinalTranscript, 1, 100, 0, true, Activity::Listening));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(100), "thinking", false, false);
    c.Handle(Ev(Kind::TtsStarted, 1, 200, 0, false));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(200), "speaking", false, false);

    // Turno nuevo: el habla anterior deja de ser vigente.
    c.Handle(Ev(Kind::TurnStarted, 2, 1000, 0, false, Activity::Listening));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(1000), "listening", false, false);

    // Callbacks locales del turno anterior no deben alterar el turno vigente.
    c.Handle(Ev(Kind::TtsStopped, 1, 1100, 0, true));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(1100), "listening", false, false);
    c.Handle(Ev(Kind::PlaybackState, 1, 1200, 0, true));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(1200), "listening", false, false);
    c.Handle(Ev(Kind::Emotion, 1, 1300, 0, true, Activity::Listening, "happy"));
    c.Handle(Ev(Kind::Tick, 1, 1350));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(1350), "listening", false, false);

    // El turno vigente sigue funcionando.
    c.Handle(Ev(Kind::FinalTranscript, 2, 1400, 0, true, Activity::Listening));
    Expect("test_new_turn_rejects_stale_callbacks", c.Resolve(1400), "thinking", false, false);
}

void TestRealtimeCaptureStartsNewTurn() {
    // Escucha simultánea: la actividad no cambia de Idle a Listening; solo
    // sube la generación local del turno.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::FinalTranscript, 1, 100, 0, false, Activity::Listening));
    Expect("test_realtime_capture_starts_new_turn", c.Resolve(100), "thinking", false, false);

    c.Handle(Ev(Kind::TurnStarted, 2, 200, 0, false, Activity::Listening));
    Expect("test_realtime_capture_starts_new_turn", c.Resolve(200), "listening", false, false);
    c.Handle(Ev(Kind::FinalTranscript, 2, 300, 0, false, Activity::Listening));
    Expect("test_realtime_capture_starts_new_turn", c.Resolve(300), "thinking", false, false);

    // Una interrupción invalida el turno visual sin tocar la actividad real.
    c.Handle(Ev(Kind::Cancelled, 2, 400, 0, false, Activity::Listening));
    Expect("test_realtime_capture_starts_new_turn", c.Resolve(400), "listening", false, false);
}

void TestEmptySttIsFilteredAtBoundary() {
    // El evento no transporta transcripciones: el adaptador descarta el STT
    // vacío y aquí solo llegan turnos válidos de la generación vigente.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::TurnStarted, 2, 100, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::FinalTranscript, 1, 200, 0, true, Activity::Listening));
    Expect("test_empty_stt_is_filtered_at_boundary", c.Resolve(200), "listening", false, false);

    // Un VAD a silencio, por sí solo, no inicia la espera de respuesta.
    c.Handle(Ev(Kind::Activity, 2, 300, 0, true, Activity::Idle));
    Expect("test_empty_stt_is_filtered_at_boundary", c.Resolve(300), "neutral", false, false);
    c.Handle(Ev(Kind::Tick, 2, 400));
    Expect("test_empty_stt_is_filtered_at_boundary", c.Resolve(400), "neutral", false, false);
}

void TestMoodExpiresAt5000Ms() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::FinalTranscript, 1, 1000, 0, true, Activity::Listening));
    Expect("test_mood_expires_at_5000_ms", c.Resolve(1000), "thinking", false, false);

    // La emoción del LLM se guarda pero no interrumpe la animación de actividad.
    c.Handle(Ev(Kind::Emotion, 1, 1100, 0, true, Activity::Listening, "happy"));
    Expect("test_mood_expires_at_5000_ms", c.Resolve(1100), "thinking", false, false);

    // La espera tiene prioridad sobre la emoción guardada.
    c.Handle(Ev(Kind::Activity, 1, 1200, 0, true, Activity::Idle));
    Expect("test_mood_expires_at_5000_ms", c.Resolve(1200), "thinking", false, false);

    c.Handle(Ev(Kind::TtsStarted, 1, 2000, 0, false));
    ExpectResolvedEmotion("test_mood_expires_at_5000_ms", c, 2000, "speaking");
    c.Handle(Ev(Kind::TtsStopped, 1, 3000, 0, true));
    ExpectResolvedEmotion("test_mood_expires_at_5000_ms", c, 3000, "happy");
    ExpectResolvedEmotion("test_mood_expires_at_5000_ms", c, 7999, "happy");
    ExpectResolvedEmotion("test_mood_expires_at_5000_ms", c, 8000, "neutral");
    ExpectResolvedEmotion("test_mood_expires_at_5000_ms", c, 9000, "neutral");

    // Escucha vigente gana a la emoción de reposo.
    AvatarController l;
    l.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    l.Handle(Ev(Kind::Emotion, 1, 100, 0, true, Activity::Listening, "happy"));
    l.Handle(Ev(Kind::TtsStarted, 1, 200, 0, false));
    l.Handle(Ev(Kind::TtsStopped, 1, 300, 0, true));
    Expect("test_mood_expires_at_5000_ms", l.Resolve(300), "listening", false, false);
}

void TestWaitExpiresAt30000Ms() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::FinalTranscript, 1, 1000, 0, true, Activity::Listening));
    Expect("test_wait_expires_at_30000_ms", c.Resolve(1000), "thinking", false, false);
    Expect("test_wait_expires_at_30000_ms", c.Resolve(30999), "thinking", false, false);
    Expect("test_wait_expires_at_30000_ms", c.Resolve(31000), "listening", false, false);

    // Una respuesta posterior válida aún puede activar speaking.
    c.Handle(Ev(Kind::TtsStarted, 1, 40000, 0, false));
    Expect("test_wait_expires_at_30000_ms", c.Resolve(40000), "speaking", false, false);
    c.Handle(Ev(Kind::TtsStopped, 1, 41000, 0, true));
    Expect("test_wait_expires_at_30000_ms", c.Resolve(41000), "listening", false, false);

    // Sin actividad de escucha la recuperación es neutral.
    AvatarController i;
    i.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    i.Handle(Ev(Kind::FinalTranscript, 1, 1000, 0, true, Activity::Idle));
    Expect("test_wait_expires_at_30000_ms", i.Resolve(31000), "neutral", false, false);
}

void TestAudioActivityExtendsMissingStopDeadline() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false));
    c.Handle(Ev(Kind::TtsStarted, 1, 0, 0, false));
    ExpectResolvedEmotion("test_audio_activity_extends_missing_stop_deadline", c, 29000,
                          "speaking");

    // Audio aceptado reinicia el plazo de recuperación visual.
    c.Handle(Ev(Kind::PlaybackState, 1, 29000, 0, true));
    ExpectResolvedEmotion("test_audio_activity_extends_missing_stop_deadline", c, 29500,
                          "speaking");
    c.Handle(Ev(Kind::AudioActivity, 1, 30000, 0, false));
    ExpectResolvedEmotion("test_audio_activity_extends_missing_stop_deadline", c, 59000,
                          "speaking");

    // 30 s sin actividad y sin audio pendiente: recuperación visual.
    c.Handle(Ev(Kind::PlaybackState, 1, 59500, 0, true));
    ExpectResolvedEmotion("test_audio_activity_extends_missing_stop_deadline", c, 60000, "neutral");
}

void TestNotificationIdIsChecked() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::NotificationStarted, 1, 100, 7, false));
    Expect("test_notification_id_is_checked", c.Resolve(100), "speaking", false, false);

    // Un drenado o un cierre de otro identificador no terminan el ciclo.
    c.Handle(Ev(Kind::NotificationFinished, 1, 200, 8, true));
    Expect("test_notification_id_is_checked", c.Resolve(200), "speaking", false, false);
    c.Handle(Ev(Kind::PlaybackState, 1, 300, 8, true));
    Expect("test_notification_id_is_checked", c.Resolve(300), "speaking", false, false);

    // El cierre llega antes que el drenado: sigue speaking.
    c.Handle(Ev(Kind::NotificationFinished, 1, 400, 7, true));
    Expect("test_notification_id_is_checked", c.Resolve(400), "speaking", false, false);

    c.Handle(Ev(Kind::PlaybackState, 1, 500, 7, true));
    Expect("test_notification_id_is_checked", c.Resolve(500), "listening", false, false);

    // Cancelar la notificación recupera la actividad real.
    AvatarController x;
    x.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    x.Handle(Ev(Kind::NotificationStarted, 1, 100, 9, false));
    Expect("test_notification_id_is_checked", x.Resolve(100), "speaking", false, false);
    x.Handle(Ev(Kind::Cancelled, 1, 200, 9, true));
    Expect("test_notification_id_is_checked", x.Resolve(200), "listening", false, false);
}

void TestNoticeSurvivesEmotion() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::NoticeStarted, 1, 100, 0, true));
    Expect("test_notice_survives_emotion", c.Resolve(100), "listening", true, false);
    c.Handle(Ev(Kind::Emotion, 1, 200, 0, true, Activity::Listening, "happy"));
    Expect("test_notice_survives_emotion", c.Resolve(200), "listening", true, false);
    c.Handle(Ev(Kind::TtsStarted, 1, 300, 0, false));
    Expect("test_notice_survives_emotion", c.Resolve(300), "speaking", true, false);
    c.Handle(Ev(Kind::NoticeCleared, 1, 400));
    Expect("test_notice_survives_emotion", c.Resolve(400), "speaking", false, false);
    c.Handle(Ev(Kind::TtsStopped, 1, 500, 0, true));
    Expect("test_notice_survives_emotion", c.Resolve(500), "listening", false, false);

    // Mantenimiento: la pantalla la posee el aviso hasta que su dueño la suelta.
    AvatarController m;
    m.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    m.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
    m.Handle(Ev(Kind::Activity, 1, 200, 0, false, Activity::Maintenance));
    Expect("test_notice_survives_emotion", m.Resolve(200), "neutral", false, true);
    m.Handle(Ev(Kind::Emotion, 1, 300, 0, false, Activity::Maintenance, "happy"));
    Expect("test_notice_survives_emotion", m.Resolve(300), "neutral", false, true);
    m.Handle(Ev(Kind::NoticeStarted, 1, 350, 0, false, Activity::Maintenance));
    Expect("test_notice_survives_emotion", m.Resolve(350), "neutral", true, true);
    m.Handle(Ev(Kind::NoticeCleared, 1, 400, 0, false, Activity::Maintenance));
    Expect("test_notice_survives_emotion", m.Resolve(400), "neutral", false, true);
    m.Handle(Ev(Kind::Activity, 1, 500, 0, false, Activity::Idle));
    Expect("test_notice_survives_emotion", m.Resolve(500), "neutral", false, false);
}

void TestAllAliasesAndInvalidNames() {
    const char* direct[] = {"neutral", "listening", "thinking",   "speaking",
                            "happy",   "sad",       "sleeping",   "pet",
                            "shake",   "look_left", "look_right", "email"};
    for (const char* name : direct) {
        ExpectAlias(name, name);
    }

    ExpectAlias("loving", "pet");
    ExpectAlias("love", "pet");
    ExpectAlias("kissy", "pet");
    ExpectAlias("embarrassed", "pet");
    ExpectAlias("crying", "sad");
    ExpectAlias("sleepy", "sleeping");
    ExpectAlias("doubtful", "shake");
    ExpectAlias("dizzy", "shake");
    ExpectAlias("confused", "shake");
    ExpectAlias("surprised", "shake");
    ExpectAlias("shocked", "shake");
    ExpectAlias("laughing", "happy");
    ExpectAlias("funny", "happy");
    ExpectAlias("silly", "happy");
    ExpectAlias("cool", "happy");
    ExpectAlias("delicious", "happy");
    ExpectAlias("confident", "happy");
    ExpectAlias("winking", "look_left");
    ExpectAlias("relaxed", "neutral");
    ExpectAlias("angry", "neutral");
    ExpectAlias("scowl", "neutral");
    ExpectAlias("talking", "speaking");
    ExpectAlias("mail", "email");
    ExpectAlias("sending", "email");

    ExpectAlias("HAPPY", "happy");
    ExpectAlias("Crying", "sad");
    ExpectAlias("LOOK_LEFT", "look_left");
    ExpectAlias("Neutral", "neutral");

    ExpectAlias("", "neutral");
    ExpectAlias("happy!", "neutral");
    ExpectAlias("happy name", "neutral");
    ExpectAlias("happy.name", "neutral");
    ExpectAlias("../etc/passwd", "neutral");
    ExpectAlias("happy\x01", "neutral");
    ExpectAlias(
        "sonr\xc3\xad"
        "e",
        "neutral");
    ExpectAlias("happy\nname", "neutral");
    ExpectAlias("ab c", "neutral");
    ExpectAlias("x", "neutral");

    // Límite de 40 caracteres ASCII.
    const std::string valid_40 = std::string(39, 'a') + "_";
    assert(valid_40.size() == 40);
    assert(IsValidAvatarEmotionName(valid_40));
    ExpectAlias(valid_40.c_str(), "neutral");
    const std::string too_long = valid_40 + "z";
    assert(too_long.size() == 41);
    assert(!IsValidAvatarEmotionName(too_long));
    ExpectAlias(too_long.c_str(), "neutral");
    assert(IsValidAvatarEmotionName("look_right"));
    assert(!IsValidAvatarEmotionName(""));
    assert(!IsValidAvatarEmotionName("happy name"));

    // Una emoción llamada speaking es solo una elección gráfica.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    c.Handle(Ev(Kind::Emotion, 1, 100, 0, true, Activity::Idle, "talking"));
    ExpectResolvedEmotion("test_all_aliases_and_invalid_names", c, 100, "speaking");
    ExpectResolvedEmotion("test_all_aliases_and_invalid_names", c, 7000, "neutral");
    c.Handle(Ev(Kind::FinalTranscript, 1, 8000, 0, true, Activity::Idle));
    ExpectResolvedEmotion("test_all_aliases_and_invalid_names", c, 8000, "thinking");
}

void TestPriorityAndNoDeadlineResets() {
    // Empates: aviso > (vista previa, que gestiona la vista) >
    // TTS/notificación > espera > escucha > emoción > neutral.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::FinalTranscript, 1, 100, 0, true, Activity::Listening));
    c.Handle(Ev(Kind::NoticeStarted, 1, 200, 0, true));
    c.Handle(Ev(Kind::Emotion, 1, 300, 0, true, Activity::Listening, "happy"));
    Expect("test_priority_and_no_deadline_resets", c.Resolve(300), "thinking", true, false);
    c.Handle(Ev(Kind::NoticeCleared, 1, 400));
    Expect("test_priority_and_no_deadline_resets", c.Resolve(400), "thinking", false, false);

    AvatarController s;
    s.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    s.Handle(Ev(Kind::FinalTranscript, 1, 100, 0, true, Activity::Listening));
    s.Handle(Ev(Kind::TtsStarted, 1, 500, 0, false));
    Expect("test_priority_and_no_deadline_resets", s.Resolve(500), "speaking", false, false);

    // Repetir el mismo estado no reinicia los plazos: la espera sigue venciendo
    // a los 30 s de FinalTranscript (100 + 30000).
    c.Handle(Ev(Kind::Activity, 1, 20000, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::Tick, 1, 25000));
    c.Handle(Ev(Kind::Activity, 1, 29000, 0, false, Activity::Listening));
    Expect("test_priority_and_no_deadline_resets", c.Resolve(30099), "thinking", false, false);
    Expect("test_priority_and_no_deadline_resets", c.Resolve(30100), "listening", false, false);
}

void TestMoodSurvivesLongReply() {
    // La ventana de reposo no corre mientras un ciclo visual posee el avatar:
    // una emocion recibida al inicio de una respuesta de mas de 5 s debe durar
    // 5 s despues de que la respuesta termine.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    c.Handle(Ev(Kind::Emotion, 1, 1000, 0, true, Activity::Idle, "happy"));
    c.Handle(Ev(Kind::TtsStarted, 1, 1100, 0, false));
    c.Handle(Ev(Kind::PlaybackState, 1, 3000, 0, false));
    c.Handle(Ev(Kind::AudioActivity, 1, 3000, 0, false));
    c.Handle(Ev(Kind::AudioActivity, 1, 7000, 0, false));
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 7500, "speaking");

    // 6 s despues de la emocion, la respuesta sigue en curso.
    c.Handle(Ev(Kind::TtsStopped, 1, 8000, 0, false));
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 8500, "speaking");

    c.Handle(Ev(Kind::PlaybackState, 1, 9000, 0, true));
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 9000, "happy");
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 10000, "happy");
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 13999, "happy");
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 14000, "neutral");
    ExpectResolvedEmotion("test_mood_survives_long_reply", c, 20000, "neutral");

    // La espera de respuesta tambien congela la ventana: la emocion reaparece
    // cuando el avatar vuelve a estar ocioso.
    AvatarController w;
    w.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    w.Handle(Ev(Kind::FinalTranscript, 1, 1000, 0, true, Activity::Idle));
    w.Handle(Ev(Kind::Emotion, 1, 1100, 0, true, Activity::Idle, "sad"));
    ExpectResolvedEmotion("test_mood_survives_long_reply", w, 1200, "thinking");
    ExpectResolvedEmotion("test_mood_survives_long_reply", w, 30999, "thinking");
    ExpectResolvedEmotion("test_mood_survives_long_reply", w, 31000, "sad");
    ExpectResolvedEmotion("test_mood_survives_long_reply", w, 35999, "sad");
    ExpectResolvedEmotion("test_mood_survives_long_reply", w, 36000, "neutral");

    // Un evento despues de la espera no revive una emocion ya vencida.
    w.Handle(Ev(Kind::Tick, 1, 40000));
    ExpectResolvedEmotion("test_mood_survives_long_reply", w, 40000, "neutral");
}

void TestStopWithoutIdleKeepsSpeaking() {
    // tts.stop con audio pendiente, sin PlaybackState(false) previo: el estado
    // del reproductor lo aporta el propio evento.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
    c.Handle(Ev(Kind::TtsStopped, 1, 200, 0, false));
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", c, 200, "speaking");
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", c, 10000, "speaking");
    c.Handle(Ev(Kind::PlaybackState, 1, 11000, 0, true));
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", c, 11000, "neutral");

    // Si nunca llega el drenado, la recuperacion visual cierra el ciclo sin
    // cortar audio: 30 s desde la ultima senal aceptada (200 + 30000).
    AvatarController r;
    r.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    r.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
    r.Handle(Ev(Kind::TtsStopped, 1, 200, 0, false));
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", r, 30199, "speaking");
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", r, 30200, "neutral");

    // Audio aceptado despues del stop extiende la ventana de recuperacion.
    AvatarController a;
    a.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    a.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
    a.Handle(Ev(Kind::TtsStopped, 1, 200, 0, false));
    a.Handle(Ev(Kind::AudioActivity, 1, 20000, 0, false));
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", a, 30199, "speaking");
    ExpectResolvedEmotion("test_stop_without_idle_keeps_speaking", a, 50200, "neutral");
}

void TestNotificationDrainedBeforeFinished() {
    // Orden real de notify_player.cc: el reproductor queda idle antes de que
    // se anuncie el fin de la notificacion.
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    c.Handle(Ev(Kind::NotificationStarted, 1, 100, 7, false));
    Expect("test_notification_drained_before_finished", c.Resolve(100), "speaking", false, false);

    c.Handle(Ev(Kind::PlaybackState, 1, 200, 7, true));
    Expect("test_notification_drained_before_finished", c.Resolve(200), "speaking", false, false);
    c.Handle(Ev(Kind::NotificationFinished, 1, 300, 7, true));
    Expect("test_notification_drained_before_finished", c.Resolve(300), "listening", false, false);

    // El mismo ciclo con el orden inverso cierra igual.
    AvatarController f;
    f.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Listening));
    f.Handle(Ev(Kind::NotificationStarted, 1, 100, 7, false));
    f.Handle(Ev(Kind::NotificationFinished, 1, 200, 7, true));
    Expect("test_notification_drained_before_finished", f.Resolve(200), "speaking", false, false);
    f.Handle(Ev(Kind::PlaybackState, 1, 300, 7, true));
    Expect("test_notification_drained_before_finished", f.Resolve(300), "listening", false, false);
}

void TestStopDrainFinishPermutations() {
    const Activity idle = Activity::Idle;
    const Activity listening = Activity::Listening;

    // (1) stop con el reproductor ya idle: el ciclo termina al instante.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, idle));
        c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
        c.Handle(Ev(Kind::TtsStopped, 1, 200, 0, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 200, "neutral");
    }

    // (2) drenado, despues stop(idle=true).
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, idle));
        c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 200, 0, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 300, 0, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 300, "speaking");
        c.Handle(Ev(Kind::TtsStopped, 1, 400, 0, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 400, "neutral");
    }

    // (3) drenado momentaneo, despues stop(idle=false): el stop anuncia audio
    // pendiente, asi que el ciclo espera al siguiente drenado.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, idle));
        c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 200, 0, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 300, 0, true));
        c.Handle(Ev(Kind::TtsStopped, 1, 400, 0, false));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 400, "speaking");
        c.Handle(Ev(Kind::PlaybackState, 1, 500, 0, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 500, "neutral");
    }

    // (4) stop(idle=false), despues drenado.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, idle));
        c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 200, 0, false));
        c.Handle(Ev(Kind::TtsStopped, 1, 300, 0, false));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 300, "speaking");
        c.Handle(Ev(Kind::PlaybackState, 1, 400, 0, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 400, "neutral");
    }

    // (5) sin stop y sin drenado: solo la recuperacion de 30 s cierra el ciclo.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, idle));
        c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 30099, "speaking");
        c.Handle(Ev(Kind::PlaybackState, 1, 30050, 0, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 30100, "neutral");
    }

    // (6) dos notificaciones seguidas con identificadores distintos.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, listening));
        c.Handle(Ev(Kind::NotificationStarted, 1, 100, 3, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 200, 3, true));
        c.Handle(Ev(Kind::NotificationFinished, 1, 300, 3, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 300, "listening");

        c.Handle(Ev(Kind::NotificationStarted, 1, 400, 4, false));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 400, "speaking");
        // Un cierre del identificador anterior ya no es vigente.
        c.Handle(Ev(Kind::NotificationFinished, 1, 500, 3, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 500, "speaking");
        c.Handle(Ev(Kind::PlaybackState, 1, 600, 4, true));
        c.Handle(Ev(Kind::NotificationFinished, 1, 700, 4, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 700, "listening");
    }

    // (7) notificacion durante una respuesta: la notificacion manda y cierra su
    // propio ciclo; el ciclo de TTS queda invalidado al terminar.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, listening));
        c.Handle(Ev(Kind::TtsStarted, 1, 100, 0, false));
        c.Handle(Ev(Kind::NotificationStarted, 1, 200, 9, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 300, 9, true));
        c.Handle(Ev(Kind::NotificationFinished, 1, 400, 9, true));
        ExpectResolvedEmotion("test_stop_drain_finish_permutations", c, 400, "listening");
    }
}

void TestNotificationPlaybackIdChecks() {
    const Activity listening = Activity::Listening;

    // (a) NotificationFinished con identificador ajeno no cierra la vigente.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, listening));
        c.Handle(Ev(Kind::NotificationStarted, 1, 100, 5, false));
        c.Handle(Ev(Kind::NotificationFinished, 1, 200, 6, true));
        Expect("test_notification_playback_id_checks", c.Resolve(200), "speaking", false, false);
        c.Handle(Ev(Kind::PlaybackState, 1, 300, 5, true));
        Expect("test_notification_playback_id_checks", c.Resolve(300), "speaking", false, false);
        c.Handle(Ev(Kind::NotificationFinished, 1, 400, 5, true));
        Expect("test_notification_playback_id_checks", c.Resolve(400), "listening", false, false);
    }

    // (b) Un drenado con identificador ajeno no drena la notificacion vigente.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, listening));
        c.Handle(Ev(Kind::NotificationStarted, 1, 100, 5, false));
        c.Handle(Ev(Kind::NotificationFinished, 1, 200, 5, true));
        Expect("test_notification_playback_id_checks", c.Resolve(200), "speaking", false, false);
        c.Handle(Ev(Kind::PlaybackState, 1, 300, 6, true));
        Expect("test_notification_playback_id_checks", c.Resolve(300), "speaking", false, false);
        c.Handle(Ev(Kind::PlaybackState, 1, 400, 5, true));
        Expect("test_notification_playback_id_checks", c.Resolve(400), "listening", false, false);
    }

    // (c) Un drenado valido pero sin cierre no cierra el ciclo.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, listening));
        c.Handle(Ev(Kind::NotificationStarted, 1, 100, 5, false));
        c.Handle(Ev(Kind::PlaybackState, 1, 200, 5, true));
        Expect("test_notification_playback_id_checks", c.Resolve(200), "speaking", false, false);
        c.Handle(Ev(Kind::PlaybackState, 1, 300, 5, true));
        Expect("test_notification_playback_id_checks", c.Resolve(300), "speaking", false, false);
    }

    // (d) Sin drenado ni cierre: recuperacion acotada a 30 s desde la senal de
    // audio (1000 + 30000), nunca antes.
    {
        AvatarController c;
        c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, listening));
        c.Handle(Ev(Kind::NotificationStarted, 1, 1000, 11, false));
        Expect("test_notification_playback_id_checks", c.Resolve(30999), "speaking", false, false);
        Expect("test_notification_playback_id_checks", c.Resolve(31000), "listening", false, false);
    }
}

void TestCancellationClearsEmotion() {
    AvatarController c;
    c.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    c.Handle(Ev(Kind::Emotion, 1, 100, 0, true, Activity::Idle, "pet"));
    ExpectResolvedEmotion("test_cancellation_clears_emotion", c, 200, "pet");

    c.Handle(Ev(Kind::Cancelled, 1, 300, 0, true, Activity::Idle));
    ExpectResolvedEmotion("test_cancellation_clears_emotion", c, 300, "neutral");
    ExpectResolvedEmotion("test_cancellation_clears_emotion", c, 4000, "neutral");
    c.Handle(Ev(Kind::TurnStarted, 2, 5000, 0, true, Activity::Idle));
    ExpectResolvedEmotion("test_cancellation_clears_emotion", c, 5100, "neutral");

    // Cancelar durante speaking tampoco deja la emocion.
    AvatarController s;
    s.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    s.Handle(Ev(Kind::Emotion, 1, 100, 0, true, Activity::Idle, "happy"));
    s.Handle(Ev(Kind::TtsStarted, 1, 200, 0, false));
    ExpectResolvedEmotion("test_cancellation_clears_emotion", s, 300, "speaking");
    s.Handle(Ev(Kind::Cancelled, 1, 400, 0, true, Activity::Idle));
    ExpectResolvedEmotion("test_cancellation_clears_emotion", s, 400, "neutral");
    ExpectResolvedEmotion("test_cancellation_clears_emotion", s, 4000, "neutral");

    // Contraste: el cierre normal de un ciclo SI conserva la emocion.
    AvatarController d;
    d.Handle(Ev(Kind::TurnStarted, 1, 0, 0, false, Activity::Idle));
    d.Handle(Ev(Kind::Emotion, 1, 100, 0, true, Activity::Idle, "sad"));
    d.Handle(Ev(Kind::TtsStarted, 1, 200, 0, false));
    d.Handle(Ev(Kind::TtsStopped, 1, 300, 0, true));
    ExpectResolvedEmotion("test_cancellation_clears_emotion", d, 300, "sad");
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases = {
        {"test_stop_requires_drained", TestStopRequiresDrained},
        {"test_new_turn_rejects_stale_callbacks", TestNewTurnRejectsStaleCallbacks},
        {"test_realtime_capture_starts_new_turn", TestRealtimeCaptureStartsNewTurn},
        {"test_empty_stt_is_filtered_at_boundary", TestEmptySttIsFilteredAtBoundary},
        {"test_mood_expires_at_5000_ms", TestMoodExpiresAt5000Ms},
        {"test_wait_expires_at_30000_ms", TestWaitExpiresAt30000Ms},
        {"test_audio_activity_extends_missing_stop_deadline",
         TestAudioActivityExtendsMissingStopDeadline},
        {"test_notification_id_is_checked", TestNotificationIdIsChecked},
        {"test_notice_survives_emotion", TestNoticeSurvivesEmotion},
        {"test_all_aliases_and_invalid_names", TestAllAliasesAndInvalidNames},
        {"test_priority_and_no_deadline_resets", TestPriorityAndNoDeadlineResets},
        {"test_mood_survives_long_reply", TestMoodSurvivesLongReply},
        {"test_stop_without_idle_keeps_speaking", TestStopWithoutIdleKeepsSpeaking},
        {"test_notification_drained_before_finished", TestNotificationDrainedBeforeFinished},
        {"test_stop_drain_finish_permutations", TestStopDrainFinishPermutations},
        {"test_notification_playback_id_checks", TestNotificationPlaybackIdChecks},
        {"test_cancellation_clears_emotion", TestCancellationClearsEmotion},
    };

    if (argc < 2) {
        std::fprintf(stderr, "usage: %s CASE\n", argv[0]);
        return 2;
    }
    const auto it = cases.find(argv[1]);
    if (it == cases.end()) {
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }
    it->second();
    return 0;
}
