#ifndef NB_GAZE_H
#define NB_GAZE_H

#include <stdint.h>

#include "nb_avatar.h"

#ifdef __cplusplus
extern "C" {
#endif

// Where the robot looks at rest (docs/FACE.md, "Olhar autônomo"). Each look
// draws a direction, a reach, a travel time and a dwell, never repeating
// either of the last two directions, and now and then comes back to the
// center. The mood changes the manner, not the face: arousal sets the pace
// and reach (and how slowly the robot blinks), valence pulls the eyes up and
// to the sides when positive, down and away when negative. Time only moves
// through nb_gaze_tick() and chance only comes from the seed.

#define NB_GAZE_CENTER 8  // direction index of a look back to the center

typedef struct {
    uint32_t rng;
    float valence, arousal;  // mood used for the next look
    nb_avatar_look_t from, to;
    uint32_t move_ms;      // travel from `from` to `to`
    uint32_t hold_ms;      // then dwell on `to`
    uint32_t elapsed_ms;   // in the current look
    int8_t last[2];        // the two latest directions, -1 = none
} nb_gaze_t;

void nb_gaze_init(nb_gaze_t *g, uint32_t seed);

// Mood in -1..1 per axis; it shapes the next look, the current one finishes.
void nb_gaze_set_mood(nb_gaze_t *g, float valence, float arousal);

void nb_gaze_tick(nb_gaze_t *g, uint32_t dt_ms);

nb_avatar_look_t nb_gaze_look(const nb_gaze_t *g);

// Multiplier for blink intervals and durations: >1 is slower (calm).
float nb_gaze_blink_pace(const nb_gaze_t *g);

#ifdef __cplusplus
}
#endif

#endif  // NB_GAZE_H
