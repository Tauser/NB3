#ifndef NB_PERSONALITY_H
#define NB_PERSONALITY_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_affect.h"

#ifdef __cplusplus
extern "C" {
#endif

// The robot's local personality (docs/VIDA.md §4): defaults in firmware,
// overridable by a small text file on the microSD. Every value is kept in a
// safe range, so a bad file can make the robot calmer or livelier but never
// strange.

typedef struct {
    nb_affect_vec_t temperament;  // resting point of the mood
    float reactivity;             // gain on every stimulus
    uint32_t recovery_ms;         // time for a mood deviation to fall to 5%
    float negative_slowdown;      // negative valence recovers this many times slower
    float initiative;             // 0..1, how often it does things on its own (stage 3)
} nb_personality_t;

// Today's Noise: slightly warm and awake, ordinary reactions, a mood that
// lasts about half a minute on the face, slow to let go of a hurt.
nb_personality_t nb_personality_default(void);

// Clamps every field into its safe range. Returns true if anything changed.
bool nb_personality_clamp(nb_personality_t *p);

typedef struct {
    int applied;   // keys read and used (possibly clamped)
    int clamped;   // of those, how many were out of range
    int rejected;  // known keys whose value was not a number
    int unknown;   // keys this firmware does not know, ignored
} nb_personality_parse_result_t;

// Reads "key = value" lines over *p, leaving fields the text does not set as
// they are. '#' starts a comment. Keys: temperament_valence,
// temperament_arousal, reactivity, recovery_s, negative_slowdown, initiative.
nb_personality_parse_result_t nb_personality_parse(const char *text, nb_personality_t *p);

// The part the mood uses.
nb_affect_personality_t nb_personality_affect(const nb_personality_t *p);

#ifdef __cplusplus
}
#endif

#endif  // NB_PERSONALITY_H
