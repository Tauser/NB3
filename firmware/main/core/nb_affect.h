#ifndef NB_AFFECT_H
#define NB_AFFECT_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// The robot's mood: a continuous valence x arousal vector in [-1, 1] that
// stimuli push and time pulls back to the temperament. It lives in RAM and
// starts at the temperament on every boot; it is independent of the
// behavioral state, so IDLE never resets it. See docs/VIDA.md §3.

typedef struct {
    float valence;  // displeasure -1 .. +1 pleasure
    float arousal;  // calm -1 .. +1 alert
} nb_affect_vec_t;

// Local personality (docs/VIDA.md §4).
typedef struct {
    nb_affect_vec_t temperament;  // resting point of the mood
    float reactivity;             // gain applied to every stimulus
    uint32_t recovery_ms;         // time for a deviation to fall to 5%
    float negative_slowdown;      // valence below 0 recovers this many times slower
} nb_affect_personality_t;

// A stimulus. Repeats of the same key inside the habituation window count
// less each time (delta x 0.5^n) and their applied magnitude per window is
// capped, so "happy" on every sentence of a reply pushes once, not forever.
typedef struct {
    const char *key;  // "origin:tag", e.g. "xiaozhi:happy", "touch:tap"
    float valence;
    float arousal;
    float cap;        // max sum of |applied| for this key per window
} nb_affect_stimulus_t;

#define NB_AFFECT_HABIT_SLOTS 16
#define NB_AFFECT_HABIT_WINDOW_MS 30000u
// Memory may move the resting point at most this far on each axis.
#define NB_AFFECT_BIAS_LIMIT 0.15f

typedef struct {
    bool used;
    uint32_t key_hash;
    uint32_t window_start_ms;
    uint8_t repeats;
    float applied;
} nb_affect_habit_t;

typedef struct {
    nb_affect_personality_t personality;
    nb_affect_vec_t mood;
    nb_affect_vec_t bias;
    nb_affect_habit_t habits[NB_AFFECT_HABIT_SLOTS];
    nb_expression_t expression;  // last resolved, kept for hysteresis
} nb_affect_t;

// Temperament (+0.10, +0.05): at peace, slightly warm and awake. Deviations
// fall to 5% in 60 s; negative valence takes twice as long.
nb_affect_personality_t nb_affect_default_personality(void);

// personality NULL uses the default.
void nb_affect_init(nb_affect_t *affect, const nb_affect_personality_t *personality);

// Applies a stimulus after habituation and reactivity. Returns the share of
// the stimulus actually applied, 0..1 (0 when habituated away or invalid).
float nb_affect_stimulate(nb_affect_t *affect, const nb_affect_stimulus_t *stimulus,
                          uint32_t now_ms);

// Replaces the memory bias on the resting point (not cumulative: calling it
// twice with the same value changes nothing). Clamped to the bias limit.
void nb_affect_set_bias(nb_affect_t *affect, float valence, float arousal);

// Pulls the mood toward temperament + bias.
void nb_affect_tick(nb_affect_t *affect, uint32_t dt_ms);

// The expression the mood shows, with hysteresis so a mood on a border does
// not flicker. NB_EXPR_NEUTRAL near the temperament.
nb_expression_t nb_affect_resolve(nb_affect_t *affect);

#ifdef __cplusplus
}
#endif

#endif  // NB_AFFECT_H
