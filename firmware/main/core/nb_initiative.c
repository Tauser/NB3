#include "nb_initiative.h"

#include <string.h>

#define MINUTE_MS 60000.0f
#define THOUGHT_MIN_MS 4000u
#define THOUGHT_SPAN_MS 2000u

static float clampf(float v, float lo, float hi) {
    if (!(v >= lo)) {
        return lo;
    }
    return v > hi ? hi : v;
}

static float mixf(float a, float b, float t) { return a + (b - a) * t; }

static uint32_t next_u32(nb_initiative_t *t) {
    t->rng = t->rng * 1664525u + 1013904223u;
    return t->rng;
}

static float next_unit(nb_initiative_t *t) {
    return (float)(next_u32(t) >> 8) / 16777216.0f;
}

// Wraparound-safe "now is at or after when".
static bool reached(uint32_t now_ms, uint32_t when_ms) {
    return (int32_t)(now_ms - when_ms) >= 0;
}

uint32_t nb_initiative_quiet_ms(float initiative) {
    return (uint32_t)(mixf(6.0f, 1.0f, clampf(initiative, 0.0f, 1.0f)) * MINUTE_MS);
}

uint32_t nb_initiative_gap_ms(float initiative) {
    return (uint32_t)(mixf(12.0f, 2.0f, clampf(initiative, 0.0f, 1.0f)) * MINUTE_MS);
}

// Gaps vary from half to one and a half of the mean.
static uint32_t draw_gap(nb_initiative_t *t) {
    return (uint32_t)((float)nb_initiative_gap_ms(t->initiative) * (0.5f + next_unit(t)));
}

void nb_initiative_init(nb_initiative_t *t, float initiative, uint32_t seed, uint32_t now_ms) {
    memset(t, 0, sizeof(*t));
    t->initiative = clampf(initiative, 0.0f, 1.0f);
    t->rng = seed ^ 0x51ED270Bu;
    t->next_ms = now_ms + nb_initiative_quiet_ms(t->initiative);
}

nb_initiative_event_t nb_initiative_update(nb_initiative_t *t, bool at_rest, uint32_t now_ms) {
    if (!at_rest) {
        // Away from rest: the quiet starts over once the robot is back.
        t->next_ms = now_ms + nb_initiative_quiet_ms(t->initiative);
        if (t->thinking) {
            t->thinking = false;
            return NB_INITIATIVE_STOP;
        }
        return NB_INITIATIVE_NONE;
    }
    if (t->thinking) {
        if (reached(now_ms, t->until_ms)) {
            t->thinking = false;
            t->next_ms = now_ms + draw_gap(t);
            return NB_INITIATIVE_STOP;
        }
        return NB_INITIATIVE_NONE;
    }
    if (t->initiative <= 0.0f || !reached(now_ms, t->next_ms)) {
        return NB_INITIATIVE_NONE;
    }
    t->duration_ms = THOUGHT_MIN_MS + next_u32(t) % (THOUGHT_SPAN_MS + 1u);
    t->until_ms = now_ms + t->duration_ms;
    t->thinking = true;
    return NB_INITIATIVE_START;
}
