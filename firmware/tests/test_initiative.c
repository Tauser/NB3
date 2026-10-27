#include "nb_initiative.h"
#include "nb_test.h"

#define MINUTE 60000u

typedef struct {
    int starts;
    uint32_t first_ms;     // 0 = none
    uint32_t total_ms;     // time spent thinking
    uint32_t shortest, longest;
} run_t;

// A quiet stretch of `minutes`, sampled every second as the shell does.
static run_t quiet(nb_initiative_t *t, uint32_t start_ms, uint32_t minutes) {
    run_t r = {0, 0, 0, UINT32_MAX, 0};
    uint32_t since = 0;
    for (uint32_t elapsed = 0; elapsed < minutes * MINUTE; elapsed += 1000) {
        const uint32_t now = start_ms + elapsed;  // may wrap, as the real clock does
        const nb_initiative_event_t e = nb_initiative_update(t, true, now);
        if (e == NB_INITIATIVE_START) {
            r.starts++;
            if (r.first_ms == 0) {
                r.first_ms = now - start_ms;
            }
            since = now;
            if (t->duration_ms < r.shortest) r.shortest = t->duration_ms;
            if (t->duration_ms > r.longest) r.longest = t->duration_ms;
        } else if (e == NB_INITIATIVE_STOP) {
            r.total_ms += now - since;
        }
    }
    return r;
}

static void test_default_waits_for_quiet_then_thinks_now_and_then(void) {
    nb_initiative_t t;
    nb_initiative_init(&t, 0.5f, 1u, 0);
    const run_t r = quiet(&t, 0, 120);
    // First thought after 3.5 min of quiet, then about every 7 min.
    NB_CHECK(r.first_ms >= 210000 && r.first_ms <= 211000);
    NB_CHECK(r.starts >= 12 && r.starts <= 24);
    NB_CHECK(r.shortest >= 4000 && r.longest <= 6000);
}

static void test_initiative_doses_how_often(void) {
    nb_initiative_t low, high, none;
    nb_initiative_init(&low, 0.1f, 2u, 0);
    nb_initiative_init(&high, 1.0f, 2u, 0);
    nb_initiative_init(&none, 0.0f, 2u, 0);
    const run_t rl = quiet(&low, 0, 180);
    const run_t rh = quiet(&high, 0, 180);
    const run_t rn = quiet(&none, 0, 180);
    NB_CHECK_EQ_INT(0, rn.starts);
    NB_CHECK(rh.starts > rl.starts * 3);
    NB_CHECK(rh.first_ms <= 61000);
    // Even the most restless robot spends little of its rest thinking.
    NB_CHECK(rh.total_ms < 180u * MINUTE / 10u);
}

static void test_leaving_rest_interrupts_and_restarts_the_quiet(void) {
    nb_initiative_t t;
    nb_initiative_init(&t, 1.0f, 3u, 0);
    uint32_t now = 0;
    while (nb_initiative_update(&t, true, now) != NB_INITIATIVE_START) {
        now += 1000;
    }
    // A touch mid-thought ends it at once.
    NB_CHECK_EQ_INT(NB_INITIATIVE_STOP, nb_initiative_update(&t, false, now + 1000));
    NB_CHECK_EQ_INT(NB_INITIATIVE_NONE, nb_initiative_update(&t, false, now + 2000));
    // Back at rest, the full quiet is needed again before the next thought.
    const uint32_t back = now + 3000;
    const run_t r = quiet(&t, back, 5);
    NB_CHECK(r.first_ms >= nb_initiative_quiet_ms(1.0f) - 3000);
}

static void test_conversation_keeps_it_away(void) {
    nb_initiative_t t;
    nb_initiative_init(&t, 1.0f, 4u, 0);
    // An hour of talking, with short pauses at rest under the quiet time.
    int starts = 0;
    for (uint32_t now = 0; now < 60 * MINUTE; now += 1000) {
        const bool at_rest = (now / 1000) % 50 >= 30;  // 30 s talk, 20 s rest
        starts += nb_initiative_update(&t, at_rest, now) == NB_INITIATIVE_START;
    }
    NB_CHECK_EQ_INT(0, starts);
}

static void test_survives_the_clock_wrapping(void) {
    nb_initiative_t t;
    const uint32_t near_wrap = UINT32_MAX - 2u * MINUTE;
    nb_initiative_init(&t, 1.0f, 5u, near_wrap);
    const run_t r = quiet(&t, near_wrap, 10);
    NB_CHECK(r.starts >= 2);
}

int main(void) {
    NB_RUN(test_default_waits_for_quiet_then_thinks_now_and_then);
    NB_RUN(test_initiative_doses_how_often);
    NB_RUN(test_leaving_rest_interrupts_and_restarts_the_quiet);
    NB_RUN(test_conversation_keeps_it_away);
    NB_RUN(test_survives_the_clock_wrapping);
    NB_TEST_MAIN_END();
}
