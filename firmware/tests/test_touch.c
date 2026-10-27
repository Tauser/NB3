#include "nb_touch.h"
#include "nb_test.h"

#define BASE 20000u
#define TOUCHED 21500u  // +7.5%: well above the 3% press threshold
#define PERIOD_MS 20u

typedef struct {
    nb_touch_t touch;
    uint32_t now;
} rig_t;

static void rig_init(rig_t *r) {
    nb_touch_config_t cfg = nb_touch_default_config();
    nb_touch_init(&r->touch, &cfg);
    r->now = 0;
    for (int i = 0; i < cfg.calib_samples; i++) {
        nb_touch_update(&r->touch, BASE, r->now);
        r->now += PERIOD_MS;
    }
}

// Feeds `count` samples of `raw`; returns the first non-NONE event seen.
static nb_touch_event_t feed(rig_t *r, uint32_t raw, int count) {
    nb_touch_event_t first = NB_TOUCH_NONE;
    for (int i = 0; i < count; i++) {
        nb_touch_event_t e = nb_touch_update(&r->touch, raw, r->now);
        r->now += PERIOD_MS;
        if (first == NB_TOUCH_NONE) {
            first = e;
        }
    }
    return first;
}

static void test_no_events_before_calibration(void) {
    nb_touch_t t;
    nb_touch_config_t cfg = nb_touch_default_config();
    nb_touch_init(&t, &cfg);
    for (int i = 0; i < cfg.calib_samples - 1; i++) {
        NB_CHECK_EQ_INT(NB_TOUCH_NONE, nb_touch_update(&t, TOUCHED, (uint32_t)i * PERIOD_MS));
        NB_CHECK(!nb_touch_is_calibrated(&t));
    }
    nb_touch_update(&t, BASE, 1000);
    NB_CHECK(nb_touch_is_calibrated(&t));
}

static void test_short_touch_is_press_then_tap(void) {
    rig_t r;
    rig_init(&r);
    NB_CHECK_EQ_INT(NB_TOUCH_PRESS, feed(&r, TOUCHED, 10));
    NB_CHECK_EQ_INT(NB_TOUCH_TAP, feed(&r, BASE, 5));
}

static void test_long_touch_is_hold_then_release(void) {
    rig_t r;
    rig_init(&r);
    NB_CHECK_EQ_INT(NB_TOUCH_PRESS, feed(&r, TOUCHED, 5));
    NB_CHECK_EQ_INT(NB_TOUCH_HOLD, feed(&r, TOUCHED, 80));      // 1.6 s more
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, TOUCHED, 80));      // hold fires once
    NB_CHECK_EQ_INT(NB_TOUCH_RELEASE, feed(&r, BASE, 5));
}

static void test_medium_touch_is_release_not_tap(void) {
    rig_t r;
    rig_init(&r);
    feed(&r, TOUCHED, 5);
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, TOUCHED, 45));      // ~1 s, below hold
    NB_CHECK_EQ_INT(NB_TOUCH_RELEASE, feed(&r, BASE, 5));
}

static void test_glitches_are_debounced(void) {
    rig_t r;
    rig_init(&r);
    for (int i = 0; i < 20; i++) {
        NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, TOUCHED, 2));  // shorter than debounce
        NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, BASE, 1));
    }
    NB_CHECK(!r.touch.touched);
}

static void test_small_rise_is_not_a_touch(void) {
    rig_t r;
    rig_init(&r);
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, BASE + BASE / 50, 100));  // +2%
}

static void test_hysteresis_keeps_contact_between_thresholds(void) {
    rig_t r;
    rig_init(&r);
    feed(&r, TOUCHED, 5);
    // +2%: below press threshold but above release threshold, still touched.
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, BASE + BASE / 50, 10));
    NB_CHECK(r.touch.touched);
}

static void test_baseline_follows_slow_drift(void) {
    rig_t r;
    rig_init(&r);
    // Untouched reading creeps up 1% over time; must not count as a touch.
    uint32_t drifted = BASE + BASE / 100;
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, drifted, 3000));
    NB_CHECK(r.touch.baseline > (float)BASE + (float)BASE / 200.0f);
    // A real touch on top of the drifted baseline is still detected.
    NB_CHECK_EQ_INT(NB_TOUCH_PRESS, feed(&r, drifted + drifted * 6 / 100, 5));
}

static void test_baseline_frozen_while_touched(void) {
    rig_t r;
    rig_init(&r);
    float before = r.touch.baseline;
    feed(&r, TOUCHED, 500);
    NB_CHECK(r.touch.baseline == before);
}

static void test_endless_contact_becomes_the_new_baseline(void) {
    rig_t r;
    rig_init(&r);
    // The pad's reading jumps and stays there (nobody touching).
    NB_CHECK_EQ_INT(NB_TOUCH_PRESS, feed(&r, TOUCHED, 5));
    NB_CHECK_EQ_INT(NB_TOUCH_HOLD, feed(&r, TOUCHED, 100));
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, TOUCHED, 2500));  // 50 s of "petting"
    NB_CHECK_EQ_INT(NB_TOUCH_STUCK, feed(&r, TOUCHED, 500));
    NB_CHECK(!r.touch.touched);
    // That level is now the untouched one: no new contact for an hour.
    NB_CHECK_EQ_INT(NB_TOUCH_NONE, feed(&r, TOUCHED, 180000));
    // A real touch on top of it is still detected.
    NB_CHECK_EQ_INT(NB_TOUCH_PRESS, feed(&r, TOUCHED + TOUCHED * 6 / 100, 5));
}

int main(void) {
    NB_RUN(test_no_events_before_calibration);
    NB_RUN(test_short_touch_is_press_then_tap);
    NB_RUN(test_long_touch_is_hold_then_release);
    NB_RUN(test_medium_touch_is_release_not_tap);
    NB_RUN(test_glitches_are_debounced);
    NB_RUN(test_small_rise_is_not_a_touch);
    NB_RUN(test_hysteresis_keeps_contact_between_thresholds);
    NB_RUN(test_baseline_follows_slow_drift);
    NB_RUN(test_baseline_frozen_while_touched);
    NB_RUN(test_endless_contact_becomes_the_new_baseline);
    NB_TEST_MAIN_END();
}
