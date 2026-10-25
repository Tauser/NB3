#ifndef NB_FACE_ANIM_H
#define NB_FACE_ANIM_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// Turns a target expression into the per-frame face pose: eases between
// expressions, blinks at random intervals and moves the mouth while the
// robot speaks. Time comes in through now_ms, randomness through the seed.
typedef struct {
    nb_face_params_t current;   // eased pose, before blink/talk overlays
    nb_face_params_t target;
    uint32_t last_ms;
    uint32_t rng;
    uint32_t next_blink_ms;
    uint32_t blink_start_ms;
    bool blinking;
    bool speaking;
    float talk_phase;           // radians
} nb_face_anim_t;

#define NB_FACE_ANIM_EASE_MS 120.0f   // time constant of the expression easing
#define NB_FACE_ANIM_BLINK_MS 160u    // full close-and-open duration
#define NB_FACE_ANIM_BLINK_MIN_MS 2000u
#define NB_FACE_ANIM_BLINK_MAX_MS 6000u
#define NB_FACE_ANIM_TALK_HZ 5.0f

void nb_face_anim_init(nb_face_anim_t *anim, nb_face_params_t start, uint32_t seed,
                       uint32_t now_ms);

void nb_face_anim_set_target(nb_face_anim_t *anim, nb_face_params_t target);

void nb_face_anim_set_speaking(nb_face_anim_t *anim, bool speaking);

// Advances to now_ms and returns the pose to draw this frame.
nb_face_params_t nb_face_anim_step(nb_face_anim_t *anim, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // NB_FACE_ANIM_H
