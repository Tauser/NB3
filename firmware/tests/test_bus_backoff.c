#include "nb_bus_backoff.h"
#include "nb_test.h"

static const nb_bus_backoff_cfg_t kCfg = {.silent_cycles = 3, .probe_period_ms = 500};

static nb_bus_backoff_t fresh(void) {
    nb_bus_backoff_t b;
    nb_bus_backoff_init(&b, &kCfg);
    return b;
}

static void silent_cycles(nb_bus_backoff_t *b, int n, uint32_t start_ms) {
    for (int i = 0; i < n; i++) {
        nb_bus_backoff_report(b, false, start_ms + 50u * (uint32_t)i);
    }
}

static void test_normal_rate_until_enough_silent_cycles(void) {
    nb_bus_backoff_t b = fresh();
    NB_CHECK(nb_bus_backoff_due(&b, 0));
    NB_CHECK(nb_bus_backoff_report(&b, false, 0) == NB_BUS_BACKOFF_NO_CHANGE);
    NB_CHECK(nb_bus_backoff_report(&b, false, 50) == NB_BUS_BACKOFF_NO_CHANGE);
    NB_CHECK(nb_bus_backoff_due(&b, 100));
    NB_CHECK(nb_bus_backoff_report(&b, false, 100) == NB_BUS_BACKOFF_ENTERED);
}

static void test_backed_off_bus_is_tried_once_per_period(void) {
    nb_bus_backoff_t b = fresh();
    silent_cycles(&b, 3, 0);  // last report at 100
    NB_CHECK(!nb_bus_backoff_due(&b, 150));
    NB_CHECK(!nb_bus_backoff_due(&b, 599));
    NB_CHECK(nb_bus_backoff_due(&b, 600));
    // A probe that stays silent schedules the next one a period later.
    NB_CHECK(nb_bus_backoff_report(&b, false, 600) == NB_BUS_BACKOFF_NO_CHANGE);
    NB_CHECK(!nb_bus_backoff_due(&b, 650));
    NB_CHECK(nb_bus_backoff_due(&b, 1100));
}

static void test_first_reply_restores_the_normal_rate(void) {
    nb_bus_backoff_t b = fresh();
    silent_cycles(&b, 5, 0);
    NB_CHECK(!nb_bus_backoff_due(&b, 300));
    NB_CHECK(nb_bus_backoff_report(&b, true, 700) == NB_BUS_BACKOFF_LEFT);
    NB_CHECK(nb_bus_backoff_due(&b, 701));
    NB_CHECK(nb_bus_backoff_report(&b, true, 750) == NB_BUS_BACKOFF_NO_CHANGE);
}

static void test_a_reply_resets_the_silent_count(void) {
    nb_bus_backoff_t b = fresh();
    silent_cycles(&b, 2, 0);
    nb_bus_backoff_report(&b, true, 100);
    silent_cycles(&b, 2, 150);
    NB_CHECK(nb_bus_backoff_due(&b, 250));  // 2 + 2 silent cycles never backed off
}

static void test_clock_wrap(void) {
    nb_bus_backoff_t b = fresh();
    silent_cycles(&b, 3, UINT32_MAX - 120u);  // last report just before the wrap
    NB_CHECK(!nb_bus_backoff_due(&b, UINT32_MAX));
    NB_CHECK(!nb_bus_backoff_due(&b, 478));  // 479 = the last report + 500, wrapped
    NB_CHECK(nb_bus_backoff_due(&b, 479));
}

static void test_config_is_clamped_and_count_saturates(void) {
    const nb_bus_backoff_cfg_t zero = {.silent_cycles = 0, .probe_period_ms = 100};
    nb_bus_backoff_t b;
    nb_bus_backoff_init(&b, &zero);
    NB_CHECK(nb_bus_backoff_report(&b, false, 0) == NB_BUS_BACKOFF_ENTERED);
    for (int i = 0; i < 1000; i++) {
        nb_bus_backoff_report(&b, false, 100u * (uint32_t)i);
    }
    NB_CHECK(b.backed_off);
    NB_CHECK(nb_bus_backoff_report(&b, true, 200000) == NB_BUS_BACKOFF_LEFT);
}

int main(void) {
    NB_RUN(test_normal_rate_until_enough_silent_cycles);
    NB_RUN(test_backed_off_bus_is_tried_once_per_period);
    NB_RUN(test_first_reply_restores_the_normal_rate);
    NB_RUN(test_a_reply_resets_the_silent_count);
    NB_RUN(test_clock_wrap);
    NB_RUN(test_config_is_clamped_and_count_saturates);
    NB_TEST_MAIN_END();
}
