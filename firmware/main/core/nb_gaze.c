#include "nb_gaze.h"

#include <math.h>
#include <string.h>

// Reach of a full look in each axis, the same vocabulary as the avatar's
// earlier fixed choreography: the head turns ~27 deg, the eyes slide in the
// face, and a vertical look moves the eyes far, as the 2.5D cue needs.
#define YAW_MAX 27.0f
#define PITCH_MAX 26.0f
#define ROLL_MAX 5.0f
#define EYE_X_MAX 12.0f
#define EYE_Y_MAX 82.0f

// About one look in four goes back to the center at neutral valence.
#define CENTER_WEIGHT 2.67f

static float clampf(float v, float lo, float hi) {
    if (!(v >= lo)) {
        return lo;
    }
    return v > hi ? hi : v;
}

static float mixf(float a, float b, float t) { return a + (b - a) * t; }

static uint32_t next_u32(nb_gaze_t *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng;
}

// Uniform in [0, 1).
static float next_unit(nb_gaze_t *g) {
    return (float)(next_u32(g) >> 8) / 16777216.0f;
}

// 0 calm .. 1 alert.
static float energy(const nb_gaze_t *g) { return (g->arousal + 1.0f) * 0.5f; }

// Direction k of 8, screen coordinates (y grows downward).
static void direction(int k, float *dx, float *dy) {
    static const float kDiag = 0.70710678f;
    static const float kDirs[8][2] = {
        {1, 0}, {kDiag, kDiag}, {0, 1}, {-kDiag, kDiag},
        {-1, 0}, {-kDiag, -kDiag}, {0, -1}, {kDiag, -kDiag},
    };
    *dx = kDirs[k][0];
    *dy = kDirs[k][1];
}

static float weight_of(const nb_gaze_t *g, int k) {
    const float v = g->valence;
    if (k == NB_GAZE_CENTER) {
        // A sad robot rarely looks straight back.
        return CENTER_WEIGHT * (v < 0.0f ? 1.0f + 0.8f * v : 1.0f);
    }
    float dx, dy;
    direction(k, &dx, &dy);
    const float up = dy < 0.0f ? -dy : 0.0f;
    const float down = dy > 0.0f ? dy : 0.0f;
    float w = 1.0f;
    if (v > 0.0f) {
        w *= (1.0f + 1.2f * v * up + 0.6f * v * fabsf(dx)) * (1.0f - 0.7f * v * down);
    } else if (v < 0.0f) {
        const float n = -v;
        w *= (1.0f + 1.2f * n * down) * (1.0f - 0.7f * n * up);
    }
    return w;
}

static bool recent(const nb_gaze_t *g, int k) {
    return g->last[0] == k || g->last[1] == k;
}

static int pick_direction(nb_gaze_t *g) {
    float weights[9];
    float total = 0.0f;
    for (int k = 0; k < 9; k++) {
        weights[k] = recent(g, k) ? 0.0f : weight_of(g, k);
        total += weights[k];
    }
    float x = next_unit(g) * total;
    for (int k = 0; k < 9; k++) {
        if (weights[k] <= 0.0f) {
            continue;
        }
        if (x < weights[k]) {
            return k;
        }
        x -= weights[k];
    }
    for (int k = 8; k >= 0; k--) {  // rounding at the very end of the range
        if (weights[k] > 0.0f) {
            return k;
        }
    }
    return NB_GAZE_CENTER;
}

static void next_look(nb_gaze_t *g) {
    const float e = energy(g);
    const int k = pick_direction(g);
    g->last[1] = g->last[0];
    g->last[0] = (int8_t)k;

    nb_avatar_look_t to;
    memset(&to, 0, sizeof(to));
    if (k != NB_GAZE_CENTER) {
        float dx, dy;
        direction(k, &dx, &dy);
        const float reach = mixf(0.55f, 1.0f, e) * (0.55f + 0.45f * next_unit(g));
        to.yaw = dx * reach * YAW_MAX;
        to.pitch = dy * reach * PITCH_MAX;
        to.roll = dx * reach * ROLL_MAX;
        to.eye_x = dx * reach * EYE_X_MAX;
        to.eye_y = dy * reach * EYE_Y_MAX;
    }
    const float hold_lo = mixf(3500.0f, 1200.0f, e);
    const float hold_hi = mixf(7000.0f, 3000.0f, e);
    const float move = mixf(700.0f, 300.0f, e) * (0.8f + 0.4f * next_unit(g));
    g->from = nb_gaze_look(g);
    g->to = to;
    g->move_ms = (uint32_t)move;
    g->hold_ms = (uint32_t)mixf(hold_lo, hold_hi, next_unit(g));
    g->elapsed_ms = 0;
}

void nb_gaze_init(nb_gaze_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ^ 0x9E3779B9u;
    g->last[0] = NB_GAZE_CENTER;  // starts at the center: the first look leaves it
    g->last[1] = -1;
    // A first dwell at the center before the first look.
    g->move_ms = 0;
    g->hold_ms = 2000;
}

void nb_gaze_set_mood(nb_gaze_t *g, float valence, float arousal) {
    g->valence = clampf(valence, -1.0f, 1.0f);
    g->arousal = clampf(arousal, -1.0f, 1.0f);
}

void nb_gaze_tick(nb_gaze_t *g, uint32_t dt_ms) {
    g->elapsed_ms += dt_ms;
    // A long tick may cross several looks; each one is drawn in turn.
    for (int guard = 0; guard < 64 && g->elapsed_ms >= g->move_ms + g->hold_ms; guard++) {
        const uint32_t over = g->elapsed_ms - (g->move_ms + g->hold_ms);
        g->elapsed_ms = g->move_ms + g->hold_ms;  // so the next one starts from `to`
        next_look(g);
        g->elapsed_ms = over;
    }
    if (g->elapsed_ms >= g->move_ms + g->hold_ms) {
        g->elapsed_ms = g->move_ms;  // a very long stall: just be at the destination
    }
}

nb_avatar_look_t nb_gaze_look(const nb_gaze_t *g) {
    if (g->elapsed_ms >= g->move_ms) {
        return g->to;
    }
    float t = (float)g->elapsed_ms / (float)g->move_ms;
    t = t * t * (3.0f - 2.0f * t);
    nb_avatar_look_t out = {
        mixf(g->from.yaw, g->to.yaw, t),     mixf(g->from.pitch, g->to.pitch, t),
        mixf(g->from.roll, g->to.roll, t),   mixf(g->from.eye_x, g->to.eye_x, t),
        mixf(g->from.eye_y, g->to.eye_y, t),
    };
    return out;
}

float nb_gaze_blink_pace(const nb_gaze_t *g) { return mixf(1.5f, 0.8f, energy(g)); }
