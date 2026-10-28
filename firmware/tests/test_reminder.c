#include <string.h>

#include "nb_reminder.h"
#include "nb_test.h"

#define FLOOR 1790000000LL
#define REAL 1792929600LL  // 2026-10-26 12:00

static nb_clock_t clk;

// A boot whose clock syncs to `wall` at 2 s, or never when wall is 0.
static void boot(uint32_t id, int64_t wall) {
    nb_clock_init(&clk, FLOOR, 0, id);
    nb_clock_sample(&clk, 10, 1000);
    if (wall != 0) {
        nb_clock_sample(&clk, wall, 2000);
    }
}

static int fire(nb_reminders_t *r, uint64_t now_ms, nb_reminder_fired_t *out) {
    return nb_reminders_update(r, &clk, now_ms, out, NB_REMINDER_MAX);
}

static void test_a_timer_fires_at_its_time_not_before(void) {
    nb_reminders_t r;
    nb_reminders_init(&r);
    boot(1, 0);  // no network: a timer still works
    uint32_t id = 0;
    NB_CHECK_EQ_INT(NB_REMINDER_OK, nb_reminders_add_in(&r, 60, "tirar o bolo", &clk, 3000, &id));
    NB_CHECK(id != 0);
    nb_reminder_fired_t f[NB_REMINDER_MAX];
    NB_CHECK_EQ_INT(0, fire(&r, 62999, f));
    NB_CHECK_EQ_INT(1, fire(&r, 63000, f));
    NB_CHECK_EQ_STR("tirar o bolo", f[0].text);
    NB_CHECK_EQ_INT(0, f[0].late_s);
    NB_CHECK_EQ_INT(0, nb_reminders_count(&r));
    NB_CHECK_EQ_INT(0, fire(&r, 70000, f));  // once
}

static void test_a_time_of_day_needs_the_clock(void) {
    nb_reminders_t r;
    nb_reminders_init(&r);
    boot(1, 0);
    NB_CHECK_EQ_INT(NB_REMINDER_NO_CLOCK, nb_reminders_add_at(&r, REAL + 3600, "x", &clk, 3000, NULL));
    boot(2, REAL);
    NB_CHECK_EQ_INT(NB_REMINDER_PAST, nb_reminders_add_at(&r, REAL - 10, "x", &clk, 3000, NULL));
    NB_CHECK_EQ_INT(NB_REMINDER_TOO_FAR,
                    nb_reminders_add_at(&r, REAL + 400LL * 24 * 3600, "x", &clk, 3000, NULL));
    NB_CHECK_EQ_INT(NB_REMINDER_OK, nb_reminders_add_at(&r, REAL + 3601, "reuniao", &clk, 3000, NULL));
    nb_reminder_fired_t f[NB_REMINDER_MAX];
    NB_CHECK_EQ_INT(0, fire(&r, 3000 + 3599 * 1000ull, f));
    NB_CHECK_EQ_INT(1, fire(&r, 3000 + 3600 * 1000ull, f));
    NB_CHECK_EQ_STR("reuniao", f[0].text);
}

static void test_a_timer_dated_by_the_sync_survives_a_reboot(void) {
    nb_reminders_t r;
    nb_reminders_init(&r);
    nb_clock_init(&clk, FLOOR, 0, 1);
    nb_clock_sample(&clk, 10, 1000);
    nb_reminders_add_in(&r, 1800, "remedio", &clk, 1000, NULL);  // due at mono 1 801 000
    nb_reminder_fired_t f[NB_REMINDER_MAX];
    fire(&r, 1000, f);
    NB_CHECK(!r.entries[0].has_wall);
    nb_clock_sample(&clk, REAL, 61000);  // the network arrives a minute later
    NB_CHECK_EQ_INT(0, fire(&r, 61000, f));
    NB_CHECK(r.entries[0].has_wall);
    NB_CHECK_EQ_INT(REAL + 1740, r.entries[0].due_wall_s);
    NB_CHECK(r.dirty);

    uint8_t blob[NB_REMINDER_BLOB_MAX];
    const size_t n = nb_reminders_encode(&r, blob, sizeof(blob));
    NB_CHECK(n > 0);
    nb_reminders_t back;
    boot(2, REAL + 600);  // restarted 10 min later
    NB_CHECK_EQ_INT(0, nb_reminders_decode(&back, blob, n));
    NB_CHECK_EQ_INT(1, nb_reminders_count(&back));
    int64_t left = 0;
    NB_CHECK(nb_reminders_remaining_s(&back.entries[0], &clk, 2000, &left));
    NB_CHECK_EQ_INT(1140, left);
    NB_CHECK_EQ_INT(1, fire(&back, 2000 + 1140 * 1000ull, f));
    // New ids keep counting up after a reload.
    uint32_t id = 0;
    nb_reminders_add_in(&back, 5, "outro", &clk, 3000, &id);
    NB_CHECK(id > 1);
}

static void test_an_undated_timer_is_dropped_at_reboot(void) {
    nb_reminders_t r;
    nb_reminders_init(&r);
    boot(1, 0);
    nb_reminders_add_in(&r, 600, "sem rede", &clk, 3000, NULL);
    uint8_t blob[NB_REMINDER_BLOB_MAX];
    const size_t n = nb_reminders_encode(&r, blob, sizeof(blob));
    nb_reminders_t back;
    boot(2, REAL);
    NB_CHECK_EQ_INT(1, nb_reminders_decode(&back, blob, n));
    NB_CHECK_EQ_INT(0, nb_reminders_count(&back));
    NB_CHECK(back.dirty);  // the drop is written back
}

static void test_what_came_due_while_off_fires_late_and_in_order(void) {
    nb_reminders_t r;
    nb_reminders_init(&r);
    boot(1, REAL);
    nb_reminders_add_in(&r, 3600, "segundo", &clk, 3000, NULL);
    nb_reminders_add_in(&r, 1800, "primeiro", &clk, 3000, NULL);
    nb_reminders_add_in(&r, 3 * 3600, "depois", &clk, 3000, NULL);
    uint8_t blob[NB_REMINDER_BLOB_MAX];
    const size_t n = nb_reminders_encode(&r, blob, sizeof(blob));
    nb_reminders_t back;
    boot(2, 0);  // back on, no network yet: nothing can be known
    nb_reminders_decode(&back, blob, n);
    nb_reminder_fired_t f[NB_REMINDER_MAX];
    NB_CHECK_EQ_INT(0, fire(&back, 2000, f));
    boot(2, REAL + 2 * 3600);  // the clock syncs two hours after the timers were set
    NB_CHECK_EQ_INT(2, fire(&back, 2000, f));
    NB_CHECK_EQ_STR("primeiro", f[0].text);
    NB_CHECK_EQ_STR("segundo", f[1].text);
    NB_CHECK(f[0].late_s > f[1].late_s && f[1].late_s > 0);
    NB_CHECK_EQ_INT(1, nb_reminders_count(&back));
}

static void test_cancel_full_and_bad_input(void) {
    nb_reminders_t r;
    nb_reminders_init(&r);
    boot(1, REAL);
    uint32_t ids[NB_REMINDER_MAX];
    for (int i = 0; i < NB_REMINDER_MAX; i++) {
        NB_CHECK_EQ_INT(NB_REMINDER_OK, nb_reminders_add_in(&r, 100 + i, "x", &clk, 3000, &ids[i]));
    }
    NB_CHECK_EQ_INT(NB_REMINDER_FULL, nb_reminders_add_in(&r, 5, "x", &clk, 3000, NULL));
    NB_CHECK(nb_reminders_cancel(&r, ids[3]));
    NB_CHECK(!nb_reminders_cancel(&r, ids[3]));
    NB_CHECK_EQ_INT(NB_REMINDER_OK, nb_reminders_add_in(&r, 5, "x", &clk, 3000, NULL));
    NB_CHECK_EQ_INT(NB_REMINDER_MAX, nb_reminders_cancel_all(&r));
    NB_CHECK_EQ_INT(0, nb_reminders_count(&r));

    NB_CHECK_EQ_INT(NB_REMINDER_INVALID, nb_reminders_add_in(&r, 0, "x", &clk, 3000, NULL));
    NB_CHECK_EQ_INT(NB_REMINDER_INVALID, nb_reminders_add_in(&r, 5, "  \n", &clk, 3000, NULL));
    NB_CHECK_EQ_INT(NB_REMINDER_INVALID, nb_reminders_add_in(&r, 5, NULL, &clk, 3000, NULL));

    // A long text is cut on a character boundary, never inside "ç".
    char text[100];
    memset(text, 'a', 62);
    strcpy(text + 62, "\xc3\xa7\xc3\xa7");
    nb_reminders_add_in(&r, 5, text, &clk, 3000, NULL);
    const size_t len = strlen(r.entries[0].text);
    NB_CHECK(len <= NB_REMINDER_TEXT_MAX - 1);
    NB_CHECK(((unsigned char)r.entries[0].text[len - 1] & 0xC0u) != 0xC0u);
}

static void test_bad_storage_leaves_it_empty(void) {
    nb_reminders_t r;
    uint8_t junk[40] = {1, 2, 3};
    NB_CHECK_EQ_INT(0, nb_reminders_decode(&r, junk, sizeof(junk)));
    NB_CHECK_EQ_INT(0, nb_reminders_count(&r));
    NB_CHECK_EQ_INT(0, nb_reminders_decode(&r, NULL, 0));
    // Truncated: header promises more entries than there are bytes.
    nb_reminders_t full;
    nb_reminders_init(&full);
    boot(1, REAL);
    nb_reminders_add_in(&full, 50, "a", &clk, 3000, NULL);
    nb_reminders_add_in(&full, 60, "b", &clk, 3000, NULL);
    uint8_t blob[NB_REMINDER_BLOB_MAX];
    const size_t n = nb_reminders_encode(&full, blob, sizeof(blob));
    nb_reminders_decode(&r, blob, n - 10);
    NB_CHECK_EQ_INT(0, nb_reminders_count(&r));
    NB_CHECK_EQ_INT(0, nb_reminders_encode(&full, blob, 10));  // too small
}

int main(void) {
    NB_RUN(test_a_timer_fires_at_its_time_not_before);
    NB_RUN(test_a_time_of_day_needs_the_clock);
    NB_RUN(test_a_timer_dated_by_the_sync_survives_a_reboot);
    NB_RUN(test_an_undated_timer_is_dropped_at_reboot);
    NB_RUN(test_what_came_due_while_off_fires_late_and_in_order);
    NB_RUN(test_cancel_full_and_bad_input);
    NB_RUN(test_bad_storage_leaves_it_empty);
    NB_TEST_MAIN_END();
}
