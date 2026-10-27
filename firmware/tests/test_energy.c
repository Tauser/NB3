#include <math.h>

#include "nb_energy.h"
#include "nb_initiative.h"
#include "nb_test.h"

#define MINUTE 60000u
#define HOUR (60u * MINUTE)

// Runs `minutes` of rest (or company) a minute at a time, the clock
// advancing from minute_of_day (or unknown when < 0).
static void live(nb_energy_t *e, uint32_t minutes, bool at_rest, int *minute_of_day) {
    for (uint32_t i = 0; i < minutes; i++) {
        nb_energy_tick(e, MINUTE, at_rest, *minute_of_day);
        if (*minute_of_day >= 0) {
            *minute_of_day = (*minute_of_day + 1) % (24 * 60);
        }
    }
}

static void test_circadian_target(void) {
    NB_CHECK(nb_energy_circadian(12 * 60) == 1.0f);
    NB_CHECK(nb_energy_circadian(3 * 60) < 0.2f);
    NB_CHECK(nb_energy_circadian(-1) == NB_ENERGY_UNKNOWN_TIME);
    // Smooth through the evening and the morning.
    float prev = 1.0f;
    for (int m = 20 * 60; m < 23 * 60; m += 10) {
        const float v = nb_energy_circadian(m);
        NB_CHECK(v <= prev && prev - v < 0.06f);
        prev = v;
    }
    NB_CHECK(nb_energy_circadian(7 * 60) > 0.4f && nb_energy_circadian(7 * 60) < 0.8f);
}

static void test_the_night_comes_slowly_and_brings_sleep(void) {
    nb_energy_t e;
    nb_energy_init(&e);
    int clock = 18 * 60;
    live(&e, 2 * 60, true, &clock);  // 18:00 -> 20:00, quiet
    NB_CHECK(e.energy > 0.9f && !nb_energy_drowsy(&e));
    live(&e, 2 * 60, true, &clock);  // -> 22:00: tiring, not yet drowsy
    NB_CHECK(e.energy < 0.6f && !nb_energy_drowsy(&e));
    live(&e, 45, true, &clock);  // -> 22:45
    NB_CHECK(nb_energy_drowsy(&e));
    NB_CHECK(nb_energy_arousal(&e) < -0.3f);
    NB_CHECK_EQ_INT(0, nb_energy_initiative_gap(&e) > 0.0f);
    live(&e, 45, true, &clock);  // -> 23:30
    NB_CHECK(nb_energy_wants_sleep(&e));
}

static void test_someone_engaging_wakes_it_up_for_a_while(void) {
    nb_energy_t e;
    nb_energy_init(&e);
    int clock = 0;
    live(&e, 3 * 60, true, &clock);  // deep night
    NB_CHECK(nb_energy_wants_sleep(&e));
    const float before = e.energy;
    live(&e, 1, false, &clock);  // the wake word
    NB_CHECK(e.energy > before + 0.2f);
    NB_CHECK(!nb_energy_wants_sleep(&e));  // the quiet starts over
    live(&e, 10, true, &clock);
    NB_CHECK(!nb_energy_wants_sleep(&e) || e.quiet_ms >= NB_ENERGY_SLEEP_QUIET_MS);
    live(&e, 60, true, &clock);
    NB_CHECK(nb_energy_wants_sleep(&e));  // and night takes it back
}

static void test_unknown_time_never_sleeps(void) {
    nb_energy_t e;
    nb_energy_init(&e);
    int clock = -1;
    live(&e, 24 * 60, true, &clock);
    NB_CHECK(!nb_energy_drowsy(&e));
    NB_CHECK(!nb_energy_wants_sleep(&e));
}

static void test_boredom_grows_alone_and_drops_with_company(void) {
    nb_energy_t e;
    nb_energy_init(&e);
    int clock = 12 * 60;
    live(&e, 10, true, &clock);
    NB_CHECK(fabsf(e.boredom - 0.5f) < 0.02f);
    const float gap_half = nb_energy_initiative_gap(&e);
    live(&e, 15, true, &clock);
    NB_CHECK(e.boredom == 1.0f);
    NB_CHECK(nb_energy_initiative_gap(&e) < gap_half);  // bored: thinks more often
    NB_CHECK(fabsf(nb_energy_initiative_gap(&e) - 0.7f) < 1e-4f);
    nb_energy_tick(&e, 1000, false, clock);  // someone comes
    NB_CHECK(e.boredom < 0.31f);
    live(&e, 1, false, &clock);
    NB_CHECK(e.boredom == 0.0f);
}

static void test_energy_scales_initiative_gaps(void) {
    // Same seed: a bored robot thinks more often than a fresh one, and a
    // drowsy one not at all.
    int counts[3];
    const float scales[3] = {1.7f, 0.7f, 0.0f};
    for (int k = 0; k < 3; k++) {
        nb_initiative_t t;
        nb_initiative_init(&t, 0.5f, 9u, 0);
        nb_initiative_set_gap_scale(&t, scales[k]);
        counts[k] = 0;
        for (uint32_t now = 0; now < 4 * HOUR; now += 1000) {
            counts[k] += nb_initiative_update(&t, true, now) == NB_INITIATIVE_START;
        }
    }
    NB_CHECK(counts[1] > counts[0] * 2);
    NB_CHECK_EQ_INT(0, counts[2]);
}

int main(void) {
    NB_RUN(test_circadian_target);
    NB_RUN(test_the_night_comes_slowly_and_brings_sleep);
    NB_RUN(test_someone_engaging_wakes_it_up_for_a_while);
    NB_RUN(test_unknown_time_never_sleeps);
    NB_RUN(test_boredom_grows_alone_and_drops_with_company);
    NB_RUN(test_energy_scales_initiative_gaps);
    NB_TEST_MAIN_END();
}
