#include "nb_head_follow.h"

#include <math.h>
#include <string.h>

nb_head_follow_cfg_t nb_head_follow_default_cfg(void) {
    nb_head_follow_cfg_t cfg = {
        .yaw_gain = 0.4f,
        .pitch_gain = 0.3f,
        .min_change_deg = 4.0f,
        .min_gap_ms = 1500,
        .follow_ms = 700,
        .ai_hold_ms = 6000,
        .tap_gap_ms = 2000,
        .pet_pitch_deg = -5.0f,
        .sleep_pitch_deg = -8.0f,
    };
    return cfg;
}

void nb_head_follow_init(nb_head_follow_t *f, const nb_head_follow_cfg_t *cfg) {
    memset(f, 0, sizeof(*f));
    f->cfg = *cfg;
}

static bool reached(uint32_t now, uint32_t due) {
    return (int32_t)(now - due) >= 0;
}

void nb_head_follow_on_ai(nb_head_follow_t *f, uint32_t now_ms) {
    f->ai_hold = true;
    f->ai_until_ms = now_ms + f->cfg.ai_hold_ms;
    f->tap_pending = false;
    f->started = false;  // the AI put the head somewhere: the next follow goes from there
}

void nb_head_follow_on_tap(nb_head_follow_t *f, uint32_t now_ms) {
    if (!f->tap_seen || reached(now_ms, f->last_tap_ms + f->cfg.tap_gap_ms)) {
        f->tap_pending = true;
        f->tap_seen = true;
        f->last_tap_ms = now_ms;
    }
}

static void look(nb_head_follow_t *f, float yaw, float pitch, uint32_t now_ms,
                 nb_head_follow_out_t *out) {
    out->move = true;
    out->kind = NB_HEAD_LOOK;
    out->yaw = yaw;
    out->pitch = pitch;
    out->min_time_ms = f->cfg.follow_ms;
    f->started = true;
    f->last_yaw = yaw;
    f->last_pitch = pitch;
    f->last_move_ms = now_ms;
}

void nb_head_follow_update(nb_head_follow_t *f, const nb_head_follow_input_t *in,
                           uint32_t now_ms, nb_head_follow_out_t *out) {
    memset(out, 0, sizeof(*out));
    if (f->ai_hold && reached(now_ms, f->ai_until_ms)) {
        f->ai_hold = false;
    }
    if (!in->can_move || f->ai_hold) {
        f->tap_pending = false;  // a reaction that cannot happen now is not kept for later
        return;
    }
    if (f->tap_pending && in->mode != NB_FOLLOW_ASLEEP) {
        f->tap_pending = false;
        out->move = true;
        out->kind = NB_HEAD_NOD;
        out->repeats = 1;
        f->last_move_ms = now_ms;  // the follow waits for the nod to end
        return;
    }
    f->tap_pending = false;

    float yaw = 0.0f, pitch = 0.0f;
    if (in->mode == NB_FOLLOW_ASLEEP) {
        pitch = f->cfg.sleep_pitch_deg;
    } else if (in->petting) {
        pitch = f->cfg.pet_pitch_deg;
    } else if (in->mode == NB_FOLLOW_AWAKE && in->face_own) {
        yaw = in->face_yaw * f->cfg.yaw_gain;
        pitch = -in->face_pitch * f->cfg.pitch_gain;  // screen down is the head's up negative
    }
    const float change = fmaxf(fabsf(yaw - f->last_yaw), fabsf(pitch - f->last_pitch));
    if (f->started && change < f->cfg.min_change_deg) {
        return;
    }
    if (f->started && !reached(now_ms, f->last_move_ms + f->cfg.min_gap_ms)) {
        return;
    }
    look(f, yaw, pitch, now_ms, out);
}
