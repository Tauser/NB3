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

void nb_overlay_update(nb_overlay_t *overlay, const nb_overlay_input_t *input, uint32_t now_ms,
                       nb_overlay_frame_t *out) {
    if (overlay == NULL || input == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!overlay->started) {
        overlay->started = true;
        overlay->last_ms = now_ms;
        overlay->activity_since_ms = now_ms;
        overlay->gesture_since_ms = now_ms;
    }

    // Smoothed level, by elapsed time so the frame rate does not matter.
    const float dt = (float)(now_ms - overlay->last_ms);
    overlay->last_ms = now_ms;
    const float target = clampf(input->audio_level, 0.0f, 1.0f);
    const float tau = target > overlay->level ? NB_OVERLAY_ATTACK_MS : NB_OVERLAY_RELEASE_MS;
    overlay->level += (target - overlay->level) * (1.0f - expf(-dt / tau));

    // A: activity.
    nb_activity_kind_t activity = NB_ACTIVITY_NONE;
    uint32_t leaving = 0u;
    if (input->speaking) {
        activity = NB_ACTIVITY_SPEECH;
        overlay->speech_ending = false;
    } else if (overlay->activity == NB_ACTIVITY_SPEECH) {
        if (!overlay->speech_ending) {
            overlay->speech_ending = true;
            overlay->speech_end_ms = now_ms;
        }
        leaving = now_ms - overlay->speech_end_ms;
        if (leaving < NB_OVERLAY_SPEECH_LINGER_MS) {
            activity = NB_ACTIVITY_SPEECH;
        }
    }
    if (activity == NB_ACTIVITY_NONE && input->listening && !input->wait_confirmed) {
        activity = NB_ACTIVITY_BARS;
    }
    if (activity != overlay->activity) {
        overlay->activity = activity;
        overlay->activity_since_ms = now_ms;
        if (activity != NB_ACTIVITY_SPEECH) {
            overlay->speech_ending = false;
        }
    }
    out->activity = activity;
    const uint32_t activity_since = now_ms - overlay->activity_since_ms;
    if (activity == NB_ACTIVITY_BARS) {
        for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
            const float wobble = fabsf(sinf(now_ms * kBarRate[i] + kBarPhase[i]));
            out->bars[i] = clampf(NB_OVERLAY_BAR_FLOOR + overlay->level * (0.45f + 0.55f * wobble),
                                  0.0f, 1.0f);
        }
    } else if (activity == NB_ACTIVITY_SPEECH) {
        out->bubble = bubble_scale(activity_since, leaving);
        for (int i = 0; i < 3; ++i) {
            const float hop =
                sinf(NB_OVERLAY_TAU * ((float)activity_since / 900.0f - (float)i * 0.13f));
            out->dots[i] = fmaxf(0.0f, hop) * (0.4f + 0.6f * overlay->level);
        }
    }

    // G: gesture. Sleep, then a confirmed wait, then the winning intention.
    nb_gesture_kind_t gesture = NB_GESTURE_NONE;
    bool after_wait = false;
    if (input->sleeping) {
        gesture = NB_GESTURE_ZS;
    } else if (input->wait_confirmed) {
        gesture = NB_GESTURE_HAND;
        after_wait = true;
    } else if (input->winner == NB_EXPR_THINKING && activity != NB_ACTIVITY_BARS) {
        gesture = NB_GESTURE_HAND;
    } else if (input->winner == NB_EXPR_LOVING) {
        gesture = NB_GESTURE_HEARTS;
    }
    if (gesture != overlay->gesture || after_wait != overlay->gesture_after_wait) {
        overlay->gesture = gesture;
        overlay->gesture_after_wait = after_wait;
        overlay->gesture_since_ms = now_ms;
    }
    out->gesture = gesture;
    out->cheeks = !input->sleeping &&
                  (input->winner == NB_EXPR_LOVING || input->winner == NB_EXPR_EMBARRASSED);
    const uint32_t since = now_ms - overlay->gesture_since_ms;
    switch (gesture) {
    case NB_GESTURE_HAND: {
        const uint32_t delay = after_wait ? 0u : NB_OVERLAY_HAND_DELAY_MS;
        if (since >= delay) {
            const uint32_t in = since - delay;
            out->hand = smooth(in / 250.0f);
            out->hand_angle =
                -7.0f * (0.5f - 0.5f * cosf(NB_OVERLAY_TAU * (float)in / 1100.0f));
        }
        break;
    }
    case NB_GESTURE_HEARTS:
        for (int i = 0; i < 2; ++i) {
            out->hearts[i] = fmodf((float)(since + (uint32_t)i * 1200u) / 2400.0f, 1.0f);
        }
        break;
    case NB_GESTURE_ZS:
        for (int i = 0; i < NB_OVERLAY_ZS; ++i) {
            // One Z at a time starts, so the first frame shows none yet.
            const uint32_t offset = (uint32_t)i * (NB_OVERLAY_Z_RISE_MS / NB_OVERLAY_ZS);
            out->zs[i] = since < offset ? 0.0f
                                        : fmodf((float)(since - offset) / NB_OVERLAY_Z_RISE_MS,
                                                1.0f);
        }
        break;
    default:
        break;
    }

    // T: technical condition.
    out->tech = input->safety_stop ? NB_TECH_SAFETY : (input->net_down ? NB_TECH_NET : NB_TECH_NONE);
}
