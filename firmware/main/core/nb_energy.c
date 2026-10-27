#include "nb_energy.h"

#include <math.h>
#include <string.h>

#define MINUTE_MS 60000.0f
#define FOLLOW_TAU_MS (15.0f * MINUTE_MS)     // energy heads to the clock's target
#define WAKE_BUMP 0.25f                        // someone engages the robot
#define BORED_FULL_MS (20.0f * MINUTE_MS)      // quiet rest to full boredom
#define UNBORED_MS (1.0f * MINUTE_MS)          // company to no boredom
#define COMPANY_KEEPS 0.3f                     // boredom left when someone arrives

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float ramp(int minute, int from, int to, float a, float b) {
    return a + (b - a) * (float)(minute - from) / (float)(to - from);
}

float nb_energy_circadian(int minute_of_day) {
    if (minute_of_day < 0 || minute_of_day >= 24 * 60) {
        return NB_ENERGY_UNKNOWN_TIME;
    }
    const int m = minute_of_day;
    if (m < 6 * 60) {
        return 0.15f;                                      // night
    } else if (m < 8 * 60) {
        return ramp(m, 6 * 60, 8 * 60, 0.15f, 1.0f);       // morning
    } else if (m < 20 * 60) {
        return 1.0f;                                       // day
    } else if (m < 23 * 60) {
        return ramp(m, 20 * 60, 23 * 60, 1.0f, 0.15f);     // evening
    }
    return 0.15f;
}

void nb_energy_init(nb_energy_t *e) {
    memset(e, 0, sizeof(*e));
    e->energy = NB_ENERGY_UNKNOWN_TIME;
    e->at_rest = true;
}

void nb_energy_tick(nb_energy_t *e, uint32_t dt_ms, bool at_rest, int minute_of_day) {
    const float dt = (float)dt_ms;
    if (!at_rest && e->at_rest) {
        e->energy = clamp01(e->energy + WAKE_BUMP);
        e->boredom *= COMPANY_KEEPS;
    }
    e->at_rest = at_rest;

    const float target = nb_energy_circadian(minute_of_day);
    e->energy += (target - e->energy) * (1.0f - expf(-dt / FOLLOW_TAU_MS));
    e->energy = clamp01(e->energy);

    if (at_rest) {
        e->quiet_ms = e->quiet_ms + dt_ms < e->quiet_ms ? UINT32_MAX : e->quiet_ms + dt_ms;
        e->boredom = clamp01(e->boredom + dt / BORED_FULL_MS);
    } else {
        e->quiet_ms = 0;
        e->boredom = clamp01(e->boredom - dt / UNBORED_MS);
    }
}

bool nb_energy_drowsy(const nb_energy_t *e) { return e->energy < NB_ENERGY_DROWSY; }

bool nb_energy_wants_sleep(const nb_energy_t *e) {
    return e->energy < NB_ENERGY_SLEEP && e->quiet_ms >= NB_ENERGY_SLEEP_QUIET_MS;
}

float nb_energy_arousal(const nb_energy_t *e) { return e->energy * 2.0f - 1.0f; }

float nb_energy_initiative_gap(const nb_energy_t *e) {
    return nb_energy_drowsy(e) ? 0.0f : 1.7f - e->boredom;
}
