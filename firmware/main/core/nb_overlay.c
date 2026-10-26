#include "nb_overlay.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define NB_OVERLAY_TAU 6.2831853f

// Level smoothing: rises fast with the voice, falls slower so bars do not
// flicker between syllables.
#define NB_OVERLAY_ATTACK_MS 40.0f
#define NB_OVERLAY_RELEASE_MS 160.0f
// Bars never vanish while listening: a low floor reads as "ears open".
#define NB_OVERLAY_BAR_FLOOR 0.12f

// Each bar moves at its own rate and phase, so the row looks like a voice
// rather than a pyramid. Rates in radians per ms.
static const float kBarRate[NB_OVERLAY_BARS] = {0.011f, 0.017f, 0.009f, 0.014f,
                                                0.010f, 0.019f, 0.013f};
static const float kBarPhase[NB_OVERLAY_BARS] = {0.0f, 1.3f, 2.1f, 0.6f, 2.9f, 1.8f, 0.9f};

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smooth(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void nb_overlay_init(nb_overlay_t *overlay) {
    if (overlay != NULL) {
        memset(overlay, 0, sizeof(*overlay));
    }
}

static nb_overlay_kind_t kind_for(nb_expression_t winner) {
    switch (winner) {
    case NB_EXPR_LISTENING: return NB_OVERLAY_LISTEN;
    case NB_EXPR_THINKING: return NB_OVERLAY_THINK;
    case NB_EXPR_LOVING: return NB_OVERLAY_AFFECTION;
    case NB_EXPR_EMBARRASSED: return NB_OVERLAY_CHEEKS;
    default: return NB_OVERLAY_NONE;
    }
}

// Bubble scale: pops past full size and settles; shrinks away when leaving.
static float bubble_scale(uint32_t since, uint32_t leaving) {
    float scale;
    if (since < 180u) {
        scale = 1.12f * smooth(since / 180.0f);
    } else if (since < 300u) {
        scale = 1.12f - 0.12f * smooth((since - 180u) / 120.0f);
    } else {
        scale = 1.0f;
    }
    if (leaving > 0u) {
        scale *= 1.0f - smooth(leaving / (float)NB_OVERLAY_SPEECH_LINGER_MS);
    }
    return scale;
}

void nb_overlay_update(nb_overlay_t *overlay, nb_expression_t winner, bool speaking,
                       float audio_level, uint32_t now_ms, nb_overlay_frame_t *out) {
    if (overlay == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!overlay->started) {
        overlay->started = true;
        overlay->last_ms = now_ms;
        overlay->since_ms = now_ms;
    }

    // Smoothed level, by elapsed time so the frame rate does not matter.
    const float dt = (float)(now_ms - overlay->last_ms);
    overlay->last_ms = now_ms;
    const float target = clampf(audio_level, 0.0f, 1.0f);
    const float tau = target > overlay->level ? NB_OVERLAY_ATTACK_MS : NB_OVERLAY_RELEASE_MS;
    overlay->level += (target - overlay->level) * (1.0f - expf(-dt / tau));

    nb_overlay_kind_t kind = kind_for(winner);
    uint32_t leaving = 0u;
    if (speaking) {
        kind = NB_OVERLAY_SPEECH;
        overlay->speech_ending = false;
    } else if (overlay->kind == NB_OVERLAY_SPEECH) {
        if (!overlay->speech_ending) {
            overlay->speech_ending = true;
            overlay->speech_end_ms = now_ms;
        }
        leaving = now_ms - overlay->speech_end_ms;
        if (leaving < NB_OVERLAY_SPEECH_LINGER_MS) {
            kind = NB_OVERLAY_SPEECH;
        }
    }
    if (kind != overlay->kind) {
        overlay->kind = kind;
        overlay->since_ms = now_ms;
        if (kind != NB_OVERLAY_SPEECH) {
            overlay->speech_ending = false;
        }
    }
    const uint32_t since = now_ms - overlay->since_ms;
    out->kind = kind;

    switch (kind) {
    case NB_OVERLAY_LISTEN:
        for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
            const float wobble = fabsf(sinf(now_ms * kBarRate[i] + kBarPhase[i]));
            out->bars[i] = clampf(NB_OVERLAY_BAR_FLOOR + overlay->level * (0.45f + 0.55f * wobble),
                                  0.0f, 1.0f);
        }
        break;
    case NB_OVERLAY_THINK:
        for (int i = 0; i < 3; ++i) {
            const float wave = sinf(NB_OVERLAY_TAU * ((float)since / 1200.0f - (float)i * 0.15f));
            out->dots[i] = 0.25f + 0.75f * fmaxf(0.0f, wave);
        }
        if (since >= NB_OVERLAY_HAND_DELAY_MS) {
            const uint32_t in = since - NB_OVERLAY_HAND_DELAY_MS;
            out->hand = smooth(in / 250.0f);
            out->hand_angle =
                -7.0f * (0.5f - 0.5f * cosf(NB_OVERLAY_TAU * (float)in / 1100.0f));
        }
        break;
    case NB_OVERLAY_SPEECH:
        out->bubble = bubble_scale(since, leaving);
        for (int i = 0; i < 3; ++i) {
            const float hop = sinf(NB_OVERLAY_TAU * ((float)since / 900.0f - (float)i * 0.13f));
            out->dots[i] = fmaxf(0.0f, hop) * (0.4f + 0.6f * overlay->level);
        }
        break;
    case NB_OVERLAY_AFFECTION:
        for (int i = 0; i < 2; ++i) {
            out->hearts[i] = fmodf((float)(since + (uint32_t)i * 1200u) / 2400.0f, 1.0f);
        }
        out->cheeks = true;
        break;
    case NB_OVERLAY_CHEEKS:
        out->cheeks = true;
        break;
    default:
        break;
    }
}
