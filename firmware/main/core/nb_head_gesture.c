#include "nb_head_gesture.h"

#include <math.h>
#include <string.h>

// A look takes about this long per degree, within these bounds: quick for a
// glance, never a jerk.
#define LOOK_MS_PER_DEG 12.0f
#define LOOK_MIN_MS 250u
#define LOOK_MAX_MS 1200u
#define NOD_DEG 8.0f
#define NOD_HALF_MS 220u
#define SHAKE_DEG 10.0f
#define SHAKE_HALF_MS 250u

static const char *const kNames[] = {"look", "center", "nod", "shake"};

const char *nb_head_move_name(nb_head_move_t move) {
    return (unsigned)move < sizeof(kNames) / sizeof(kNames[0]) ? kNames[move] : "unknown";
}

bool nb_head_move_from_name(const char *name, nb_head_move_t *move) {
    for (unsigned i = 0; name != NULL && i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (strcmp(name, kNames[i]) == 0) {
            *move = (nb_head_move_t)i;
            return true;
        }
    }
    return false;
}

static bool geometry_valid(const nb_head_geometry_t *g) {
    return g != NULL && g->counts_per_deg > 0.0f && g->pan_min <= g->pan_max &&
           g->tilt_min <= g->tilt_max && (g->pan_sign == 1 || g->pan_sign == -1) &&
           (g->tilt_sign == 1 || g->tilt_sign == -1);
}

static uint16_t clamp_counts(float v, uint16_t lo, uint16_t hi, bool *clamped) {
    if (v < (float)lo) {
        *clamped = true;
        return lo;
    }
    if (v > (float)hi) {
        *clamped = true;
        return hi;
    }
    return (uint16_t)lroundf(v);
}

static uint16_t pan_at(const nb_head_geometry_t *g, float yaw_deg, bool *clamped) {
    const float v = (float)g->center_pan + (float)g->pan_sign * yaw_deg * g->counts_per_deg;
    return clamp_counts(v, g->pan_min, g->pan_max, clamped);
}

static uint16_t tilt_at(const nb_head_geometry_t *g, float pitch_deg, bool *clamped) {
    const float v = (float)g->center_tilt + (float)g->tilt_sign * pitch_deg * g->counts_per_deg;
    return clamp_counts(v, g->tilt_min, g->tilt_max, clamped);
}

float nb_head_yaw_deg(const nb_head_geometry_t *g, uint16_t pan) {
    return geometry_valid(g) ? ((float)pan - (float)g->center_pan) * (float)g->pan_sign /
                                   g->counts_per_deg
                             : 0.0f;
}

float nb_head_pitch_deg(const nb_head_geometry_t *g, uint16_t tilt) {
    return geometry_valid(g) ? ((float)tilt - (float)g->center_tilt) * (float)g->tilt_sign /
                                   g->counts_per_deg
                             : 0.0f;
}

static void add(nb_head_plan_t *p, uint16_t pan, uint16_t tilt, uint32_t ms) {
    if (p->count < NB_HEAD_WAYPOINTS_MAX) {
        p->points[p->count++] = (nb_head_waypoint_t){pan, tilt, (uint16_t)ms};
        p->total_ms += ms;
    }
}

static uint32_t look_ms(const nb_head_geometry_t *g, uint16_t pan_from, uint16_t tilt_from,
                        uint16_t pan_to, uint16_t tilt_to) {
    const float dp = fabsf((float)pan_to - (float)pan_from);
    const float dt = fabsf((float)tilt_to - (float)tilt_from);
    const float deg = (dp > dt ? dp : dt) / g->counts_per_deg;
    const float ms = deg * LOOK_MS_PER_DEG;
    return ms < (float)LOOK_MIN_MS ? LOOK_MIN_MS : (ms > (float)LOOK_MAX_MS ? LOOK_MAX_MS
                                                                             : (uint32_t)ms);
}

bool nb_head_plan(const nb_head_geometry_t *g, nb_head_move_t move, float yaw_deg,
                  float pitch_deg, int repeats, uint16_t pan_now, uint16_t tilt_now,
                  nb_head_plan_t *out) {
    if (out == NULL || !geometry_valid(g) || !isfinite(yaw_deg) || !isfinite(pitch_deg)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    repeats = repeats < 1 ? 1 : (repeats > NB_HEAD_REPEATS_MAX ? NB_HEAD_REPEATS_MAX : repeats);
    // A gesture starts from where the head is, kept inside the limits.
    bool ignored = false;
    const uint16_t pan0 = clamp_counts((float)pan_now, g->pan_min, g->pan_max, &ignored);
    const uint16_t tilt0 = clamp_counts((float)tilt_now, g->tilt_min, g->tilt_max, &ignored);
    switch (move) {
        case NB_HEAD_CENTER:
            yaw_deg = 0.0f;
            pitch_deg = 0.0f;
            // fall through
        case NB_HEAD_LOOK: {
            const uint16_t pan = pan_at(g, yaw_deg, &out->clamped);
            const uint16_t tilt = tilt_at(g, pitch_deg, &out->clamped);
            add(out, pan, tilt, look_ms(g, pan0, tilt0, pan, tilt));
            return true;
        }
        case NB_HEAD_NOD: {
            const float pitch0 = nb_head_pitch_deg(g, tilt0);
            const uint16_t down = tilt_at(g, pitch0 - NOD_DEG, &out->clamped);
            for (int i = 0; i < repeats; i++) {
                add(out, pan0, down, NOD_HALF_MS);
                add(out, pan0, tilt0, NOD_HALF_MS);
            }
            return true;
        }
        case NB_HEAD_SHAKE: {
            const float yaw0 = nb_head_yaw_deg(g, pan0);
            const uint16_t left = pan_at(g, yaw0 + SHAKE_DEG, &out->clamped);
            const uint16_t right = pan_at(g, yaw0 - SHAKE_DEG, &out->clamped);
            add(out, left, tilt0, SHAKE_HALF_MS);
            for (int i = 0; i < repeats; i++) {
                add(out, right, tilt0, 2 * SHAKE_HALF_MS);
                add(out, i + 1 < repeats ? left : pan0, tilt0,
                    i + 1 < repeats ? 2 * SHAKE_HALF_MS : SHAKE_HALF_MS);
            }
            return true;
        }
        default:
            return false;
    }
}
