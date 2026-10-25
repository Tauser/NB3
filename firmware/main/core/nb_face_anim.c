#include "nb_face_anim.h"

#include <math.h>

#define TWO_PI 6.28318530718f

static uint32_t next_random(uint32_t *state) {
    // xorshift32: cheap, deterministic for a given seed, never sticks at 0.
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t blink_interval(nb_face_anim_t *anim) {
    uint32_t span = NB_FACE_ANIM_BLINK_MAX_MS - NB_FACE_ANIM_BLINK_MIN_MS;
    return NB_FACE_ANIM_BLINK_MIN_MS + next_random(&anim->rng) % (span + 1u);
}

void nb_face_anim_init(nb_face_anim_t *anim, nb_face_params_t start, uint32_t seed,
                       uint32_t now_ms) {
    anim->current = start;
    anim->target = start;
    anim->last_ms = now_ms;
    anim->rng = seed != 0u ? seed : 0x9E3779B9u;
    anim->blinking = false;
    anim->blink_start_ms = 0;
    anim->speaking = false;
    anim->talk_phase = 0.0f;
    anim->next_blink_ms = now_ms + blink_interval(anim);
}

void nb_face_anim_set_target(nb_face_anim_t *anim, nb_face_params_t target) {
    anim->target = target;
}

void nb_face_anim_set_speaking(nb_face_anim_t *anim, bool speaking) {
    if (speaking && !anim->speaking) {
        anim->talk_phase = 0.0f;
    }
    anim->speaking = speaking;
}

static float ease(float current, float target, float k) {
    return current + (target - current) * k;
}

// 1 when open, dipping to 0 at the middle of the blink.
static float blink_factor(const nb_face_anim_t *anim, uint32_t now_ms) {
    uint32_t t = now_ms - anim->blink_start_ms;
    float half = NB_FACE_ANIM_BLINK_MS / 2.0f;
    float x = (float)t < half ? (float)t / half : ((float)NB_FACE_ANIM_BLINK_MS - (float)t) / half;
    return 1.0f - (x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x);
}

nb_face_params_t nb_face_anim_step(nb_face_anim_t *anim, uint32_t now_ms) {
    uint32_t dt = now_ms - anim->last_ms;
    anim->last_ms = now_ms;

    float k = 1.0f - expf(-(float)dt / NB_FACE_ANIM_EASE_MS);
    anim->current.eye_open = ease(anim->current.eye_open, anim->target.eye_open, k);
    anim->current.eye_smile = ease(anim->current.eye_smile, anim->target.eye_smile, k);
    anim->current.brow_tilt = ease(anim->current.brow_tilt, anim->target.brow_tilt, k);
    anim->current.mouth_curve = ease(anim->current.mouth_curve, anim->target.mouth_curve, k);
    anim->current.mouth_open = ease(anim->current.mouth_open, anim->target.mouth_open, k);
    anim->current.blush = ease(anim->current.blush, anim->target.blush, k);

    nb_face_params_t pose = anim->current;

    if (!anim->blinking && (int32_t)(now_ms - anim->next_blink_ms) >= 0) {
        anim->blinking = true;
        anim->blink_start_ms = now_ms;
    }
    if (anim->blinking) {
        if (now_ms - anim->blink_start_ms >= NB_FACE_ANIM_BLINK_MS) {
            anim->blinking = false;
            anim->next_blink_ms = now_ms + blink_interval(anim);
        } else {
            pose.eye_open *= blink_factor(anim, now_ms);
        }
    }

    if (anim->speaking) {
        anim->talk_phase += TWO_PI * NB_FACE_ANIM_TALK_HZ * (float)dt / 1000.0f;
        anim->talk_phase = fmodf(anim->talk_phase, TWO_PI);
        float talk = 0.15f + 0.45f * fabsf(sinf(anim->talk_phase));
        if (talk > pose.mouth_open) {
            pose.mouth_open = talk;
        }
    }
    return pose;
}
