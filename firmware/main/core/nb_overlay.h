#ifndef NB_OVERLAY_H
#define NB_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// The face V2 overlay layer (docs/FACE.md): one adornment at a time drawn
// over the eyes. This core picks which one and computes its animation; the
// display only draws the numbers it gets.

typedef enum {
    NB_OVERLAY_NONE = 0,
    NB_OVERLAY_LISTEN,     // audio bars following the microphone
    NB_OVERLAY_THINK,      // thought dots, then the hand on the chin
    NB_OVERLAY_SPEECH,     // speech bubble with dots in the voice's rhythm
    NB_OVERLAY_AFFECTION,  // rising hearts and cheeks
    NB_OVERLAY_CHEEKS,     // cheeks only
    NB_OVERLAY_SLEEP,      // Zs drifting up while asleep
} nb_overlay_kind_t;

#define NB_OVERLAY_ZS 3
// Each Z takes this long to rise and fade; they follow one another.
#define NB_OVERLAY_Z_RISE_MS 3300u

#define NB_OVERLAY_BARS 7

// Thinking shows the hand only once the wait gets long.
#define NB_OVERLAY_HAND_DELAY_MS 1000u
// The bubble lingers this long after the voice stops, shrinking away.
#define NB_OVERLAY_SPEECH_LINGER_MS 500u

typedef struct {
    nb_overlay_kind_t kind;
    float bars[NB_OVERLAY_BARS];  // LISTEN: bar heights, 0..1
    float dots[3];                // THINK: brightness 0..1; SPEECH: lift 0..1
    float hand;                   // THINK: 0 hidden .. 1 in place
    float hand_angle;             // THINK: nod, degrees
    float bubble;                 // SPEECH: bubble scale, 0 hidden .. ~1.1
    float hearts[2];              // AFFECTION: rise phase 0..1 of each heart
    bool cheeks;                  // AFFECTION, CHEEKS
    float zs[NB_OVERLAY_ZS];      // SLEEP: rise phase 0..1 of each Z
} nb_overlay_frame_t;

typedef struct {
    nb_overlay_kind_t kind;
    uint32_t since_ms;       // when the current kind began
    uint32_t speech_end_ms;  // when the voice stopped, valid while speech_ending
    bool speech_ending;
    uint32_t last_ms;
    float level;             // smoothed audio level 0..1
    bool started;
    bool sleeping;
} nb_overlay_t;

void nb_overlay_init(nb_overlay_t *overlay);

// Asleep, the Zs show whatever the expression (it is the sleepy one).
void nb_overlay_set_sleeping(nb_overlay_t *overlay, bool sleeping);

// Speaking wins over everything, then sleep; otherwise the winning
// expression decides.
// audio_level (0..1) is the microphone while listening and the speaker while
// speaking. Writes the frame to draw now.
void nb_overlay_update(nb_overlay_t *overlay, nb_expression_t winner, bool speaking,
                       float audio_level, uint32_t now_ms, nb_overlay_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif  // NB_OVERLAY_H
