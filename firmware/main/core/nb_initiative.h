#ifndef NB_INITIATIVE_H
#define NB_INITIATIVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initiative at rest (docs/FACE.md): after some quiet the robot now and then
// "thinks on its own" for a few seconds, more often the higher the
// personality's initiative (0 = never). Any time away from rest restarts the
// quiet count and ends a thought at once. The shell feeds whether the robot
// is at rest and the time; this core says when a thought starts and stops.

typedef enum {
    NB_INITIATIVE_NONE = 0,
    NB_INITIATIVE_START,  // show the thought for nb_initiative_t.duration_ms
    NB_INITIATIVE_STOP,   // the thought ended or was interrupted
} nb_initiative_event_t;

typedef struct {
    float initiative;     // 0..1
    uint32_t rng;
    uint32_t next_ms;     // when the next thought starts
    uint32_t until_ms;    // when the current one ends
    uint32_t duration_ms; // of the current one
    bool thinking;
} nb_initiative_t;

void nb_initiative_init(nb_initiative_t *t, float initiative, uint32_t seed, uint32_t now_ms);

nb_initiative_event_t nb_initiative_update(nb_initiative_t *t, bool at_rest, uint32_t now_ms);

// Quiet needed before the first thought, and the mean gap between them.
uint32_t nb_initiative_quiet_ms(float initiative);
uint32_t nb_initiative_gap_ms(float initiative);

#ifdef __cplusplus
}
#endif

#endif  // NB_INITIATIVE_H
