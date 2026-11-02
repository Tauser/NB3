#ifndef NB_OVERLAY_H
#define NB_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// The face V2 overlay layer (docs/FACE.md, docs/PLANO-FACE-E-COMPORTAMENTO).
// It sits after the arbiter: it never chooses the expression, it only decides
// what is drawn over the eyes, in three channels that do not depend on one
// another. Each channel shows one thing at a time:
//   A activity   listening bars or the speech bubble, from the real audio state
//   G gesture    the hand, hearts or Zs, from sleep, a confirmed wait or the
//                winning intention
//   T technical  the network or safety-stop mark, from health flags
// This core computes each channel's animation; the display draws the numbers
// it gets.

typedef enum {
    NB_ACTIVITY_NONE = 0,
    NB_ACTIVITY_BARS,    // audio bars following the microphone
    NB_ACTIVITY_SPEECH,  // speech bubble with dots in the voice's rhythm
} nb_activity_kind_t;

typedef enum {
    NB_GESTURE_NONE = 0,
    NB_GESTURE_HAND,    // the hand on the chin
    NB_GESTURE_HEARTS,  // rising hearts
    NB_GESTURE_ZS,      // Zs drifting up while asleep
} nb_gesture_kind_t;

typedef enum {
    NB_TECH_NONE = 0,
    NB_TECH_NET,     // the last attempt to reach the server failed
    NB_TECH_SAFETY,  // the motion safety stopped the servos
} nb_tech_kind_t;

#define NB_OVERLAY_ZS 3
// Each Z takes this long to rise and fade; they follow one another.
#define NB_OVERLAY_Z_RISE_MS 3300u

#define NB_OVERLAY_BARS 7

// A thinking intention shows the hand only once the thought gets long. A
// confirmed wait is already long: its own delay belongs to whoever confirms it.
#define NB_OVERLAY_HAND_DELAY_MS 1000u
// Listening bars: shown for this long after the microphone opens, in case the
// person has not started yet, and then only while there is voice plus a short
// tail. When the person goes quiet the bars close.
#define NB_OVERLAY_BARS_ATTENTION_MS 8000u
#define NB_OVERLAY_BARS_TAIL_MS 500u
// The bubble lingers this long after the voice stops, shrinking away.
#define NB_OVERLAY_SPEECH_LINGER_MS 500u

typedef struct {
    nb_expression_t winner;  // what the arbiter resolved; read only by G
    bool listening;          // the microphone is really open for the user
    bool voice;              // the person is speaking now (the device's VAD)
    bool speaking;           // the robot's voice is playing
    bool wait_confirmed;     // the server has the question and has not answered
    bool sleeping;
    bool net_down;
    bool safety_stop;
    float audio_level;       // microphone while listening, speaker while speaking, 0..1
} nb_overlay_input_t;

typedef struct {
    nb_activity_kind_t activity;
    nb_gesture_kind_t gesture;
    nb_tech_kind_t tech;
    bool cheeks;                  // part of the pose: loving and embarrassed
    float bars[NB_OVERLAY_BARS];  // BARS: bar heights, 0..1
    float dots[3];                // SPEECH: lift 0..1
    float bubble;                 // SPEECH: bubble scale, 0 hidden .. ~1.1
    float hand;                   // HAND: 0 hidden .. 1 in place
    float hand_angle;             // HAND: nod, degrees
    float hearts[2];              // HEARTS: rise phase 0..1 of each heart
    float zs[NB_OVERLAY_ZS];      // ZS: rise phase 0..1 of each Z
} nb_overlay_frame_t;

typedef struct {
    nb_activity_kind_t activity;
    nb_gesture_kind_t gesture;
    bool gesture_after_wait;   // the hand came from a confirmed wait
    uint32_t activity_since_ms;
    uint32_t gesture_since_ms;
    bool was_listening;
    bool heard_voice;          // the person has spoken in this listening spell
    uint32_t listen_start_ms;
    uint32_t last_voice_ms;
    uint32_t speech_end_ms;    // when the voice stopped, valid while speech_ending
    bool speech_ending;
    uint32_t last_ms;
    float level;               // smoothed audio level 0..1
    bool started;
} nb_overlay_t;

void nb_overlay_init(nb_overlay_t *overlay);

// Writes the frame to draw now. Rules:
//  - A: the robot's voice wins; the bubble lingers briefly after it. Otherwise
//    the bars follow the person: from the moment the microphone opens (for
//    NB_OVERLAY_BARS_ATTENTION_MS, until they speak) and then while they
//    speak and for a short tail, whatever the face expression says. A
//    confirmed wait hands the place to the hand.
//  - G: sleep, then a confirmed wait, then the winning intention. The hand of
//    a thinking intention gives way to the bars while listening.
//  - T: the safety stop wins over the network mark.
//  - An expression never invents a microphone or a wait state.
void nb_overlay_update(nb_overlay_t *overlay, const nb_overlay_input_t *input, uint32_t now_ms,
                       nb_overlay_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif  // NB_OVERLAY_H
