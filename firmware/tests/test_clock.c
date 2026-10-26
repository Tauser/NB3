#include "nb_clock.h"
#include "nb_test.h"

// A real date well past the floor: 2026-10-26 12:00 (local time as UTC).
#define FLOOR 1790000000LL
#define REAL 1792929600LL
#define HOUR 3600LL
#define YEAR (365LL * 24 * HOUR)

static void test_below_floor_is_invalid(void) {
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, 0, 1);
    nb_clock_sample(&c, 5, 5000);  // power-on: the clock starts near 1970
    NB_CHECK_EQ_INT(NB_CLOCK_INVALID, nb_clock_level(&c));
    nb_stamp_t s = nb_clock_stamp(&c, 5000);
    NB_CHECK(!s.has_wall);
    int64_t age;
    NB_CHECK(!nb_clock_age_s(&c, &s, 6000, &age));
}

static void test_clock_kept_over_soft_reset_is_only_plausible(void) {
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, REAL - 60, 2);
    nb_clock_sample(&c, REAL, 100);
    NB_CHECK_EQ_INT(NB_CLOCK_PLAUSIBLE, nb_clock_level(&c));
    nb_clock_sample(&c, REAL + 1, 1100);  // ticking along, no jump
    NB_CHECK_EQ_INT(NB_CLOCK_PLAUSIBLE, nb_clock_level(&c));
    NB_CHECK(!nb_clock_stamp(&c, 1100).has_wall);  // plausible is not written down
    int64_t now;
    NB_CHECK(!nb_clock_now_s(&c, 1100, &now));
}

static void test_plausible_behind_last_seen_is_invalid(void) {
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, REAL, 3);
    nb_clock_sample(&c, REAL - HOUR, 100);
    NB_CHECK_EQ_INT(NB_CLOCK_INVALID, nb_clock_level(&c));
}

static void test_sync_mid_boot_converts_earlier_records(void) {
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, 0, 4);
    nb_clock_sample(&c, 10, 10000);
    nb_stamp_t early = nb_clock_stamp(&c, 12000);  // made before the sync
    NB_CHECK(!early.has_wall);
    nb_clock_sample(&c, 11, 11000);
    NB_CHECK(nb_clock_sample(&c, REAL, 20000));  // xiaozhi sets the clock
    NB_CHECK_EQ_INT(NB_CLOCK_SYNCED, nb_clock_level(&c));
    NB_CHECK(c.last_seen_dirty);
    NB_CHECK_EQ_INT(REAL, c.last_seen_s);
    NB_CHECK(nb_clock_rebase(&c, &early));
    NB_CHECK(early.has_wall);
    NB_CHECK_EQ_INT(REAL - 8, early.wall_s);  // 8 s before the sync sample
    NB_CHECK(!nb_clock_rebase(&c, &early));   // already right
    int64_t age;
    NB_CHECK(nb_clock_age_s(&c, &early, 30000, &age));
    NB_CHECK_EQ_INT(18, age);
}

static void test_future_time_corrected_back_in_the_same_boot(void) {
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, 0, 5);
    nb_clock_sample(&c, 0, 0);
    nb_clock_sample(&c, REAL + YEAR, 5000);  // a server a year ahead
    nb_stamp_t s = nb_clock_stamp(&c, 6000);
    NB_CHECK_EQ_INT(REAL + YEAR + 1, s.wall_s);
    NB_CHECK(nb_clock_sample(&c, REAL, 10000));  // corrected back
    NB_CHECK_EQ_INT(NB_CLOCK_SYNCED, nb_clock_level(&c));
    NB_CHECK_EQ_INT(2, c.syncs);
    NB_CHECK_EQ_INT(REAL, c.last_seen_s);  // the lower synced time wins
    NB_CHECK(nb_clock_rebase(&c, &s));
    NB_CHECK_EQ_INT(REAL - 4, s.wall_s);
    int64_t age;
    NB_CHECK(nb_clock_age_s(&c, &s, 10000, &age));
    NB_CHECK_EQ_INT(4, age);
}

static void test_future_time_corrected_in_the_next_boot(void) {
    // The previous boot synced a year ahead and wrote down a record.
    nb_stamp_t old = {6, 5000, REAL + YEAR, true};
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, REAL + YEAR, 7);  // last seen: the wrong future
    nb_clock_sample(&c, REAL, 100);  // clock kept over a soft reset, behind last seen
    NB_CHECK_EQ_INT(NB_CLOCK_INVALID, nb_clock_level(&c));
    nb_clock_sample(&c, 3, 200);      // power cycled instead...
    nb_clock_sample(&c, REAL, 5000);  // ...and the right time arrives
    NB_CHECK_EQ_INT(NB_CLOCK_SYNCED, nb_clock_level(&c));
    NB_CHECK_EQ_INT(REAL, c.last_seen_s);  // replaced, though lower
    NB_CHECK(c.last_seen_dirty);
    NB_CHECK(!nb_clock_rebase(&c, &old));  // another boot: no monotonic link
    int64_t age;
    NB_CHECK(nb_clock_age_s(&c, &old, 6000, &age));
    NB_CHECK_EQ_INT(0, age);  // in the future: age 0 until time catches up
}

static void test_zone_changes_keep_the_clock_valid(void) {
    static const int64_t kShifts[] = {HOUR, -HOUR, 14 * HOUR, -14 * HOUR, 3 * HOUR};
    for (size_t i = 0; i < sizeof(kShifts) / sizeof(kShifts[0]); i++) {
        nb_clock_t c;
        nb_clock_init(&c, FLOOR, 0, 8);
        nb_clock_sample(&c, 0, 0);
        nb_clock_sample(&c, REAL, 1000);
        nb_stamp_t before = nb_clock_stamp(&c, 2000);
        nb_stamp_t other_boot = {99, 0, REAL - 10, true};
        nb_clock_sample(&c, REAL + 9 + kShifts[i], 10000);  // a zone change
        NB_CHECK_EQ_INT(NB_CLOCK_SYNCED, nb_clock_level(&c));
        int64_t age;
        NB_CHECK(nb_clock_age_s(&c, &other_boot, 10000, &age));
        NB_CHECK(age >= 0);
        NB_CHECK(nb_clock_age_s(&c, &before, 10000, &age));
        NB_CHECK(age >= 0);
        // This boot's record follows the new offset: its age stays 8 s.
        nb_clock_rebase(&c, &before);
        NB_CHECK(nb_clock_age_s(&c, &before, 10000, &age));
        NB_CHECK_EQ_INT(8, age);
    }
}

static void test_age_includes_the_time_switched_off(void) {
    nb_stamp_t yesterday = {9, 70000, REAL - 5 * HOUR, true};
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, REAL - 5 * HOUR, 10);
    nb_clock_sample(&c, 0, 0);
    nb_clock_sample(&c, REAL, 3000);
    int64_t age;
    NB_CHECK(nb_clock_age_s(&c, &yesterday, 3000, &age));
    NB_CHECK_EQ_INT(5 * HOUR, age);
}

static void test_last_seen_moves_forward_with_time(void) {
    nb_clock_t c;
    nb_clock_init(&c, FLOOR, 0, 11);
    nb_clock_sample(&c, 0, 0);
    nb_clock_sample(&c, REAL, 1000);
    c.last_seen_dirty = false;
    nb_clock_sample(&c, REAL + 600, 601000);
    NB_CHECK(c.last_seen_dirty);
    NB_CHECK_EQ_INT(REAL + 600, c.last_seen_s);
}

static void test_null_is_safe(void) {
    nb_clock_init(NULL, FLOOR, 0, 0);
    NB_CHECK(!nb_clock_sample(NULL, REAL, 0));
    NB_CHECK_EQ_INT(NB_CLOCK_INVALID, nb_clock_level(NULL));
    NB_CHECK(!nb_clock_stamp(NULL, 0).has_wall);
    nb_stamp_t s = {0, 0, 0, false};
    NB_CHECK(!nb_clock_rebase(NULL, &s));
    int64_t v;
    NB_CHECK(!nb_clock_age_s(NULL, &s, 0, &v));
    NB_CHECK(!nb_clock_now_s(NULL, 0, &v));
    NB_CHECK_EQ_STR("synced", nb_clock_level_name(NB_CLOCK_SYNCED));
}

int main(void) {
    NB_RUN(test_below_floor_is_invalid);
    NB_RUN(test_clock_kept_over_soft_reset_is_only_plausible);
    NB_RUN(test_plausible_behind_last_seen_is_invalid);
    NB_RUN(test_sync_mid_boot_converts_earlier_records);
    NB_RUN(test_future_time_corrected_back_in_the_same_boot);
    NB_RUN(test_future_time_corrected_in_the_next_boot);
    NB_RUN(test_zone_changes_keep_the_clock_valid);
    NB_RUN(test_age_includes_the_time_switched_off);
    NB_RUN(test_last_seen_moves_forward_with_time);
    NB_RUN(test_null_is_safe);
    NB_TEST_MAIN_END();
}
