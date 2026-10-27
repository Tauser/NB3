#include <math.h>

#include "nb_gaze.h"
#include "nb_test.h"

typedef struct {
    int looks;
    int center, up, down;
    double hold_ms, reach;
    float max_step;  // largest eye move in one 20 ms tick
    bool repeated;   // a direction equal to one of the two before it
    bool out_of_range;
} stats_t;

// Runs n looks at a fixed mood and gathers what they did.
static stats_t run(uint32_t seed, float valence, float arousal, int n) {
    nb_gaze_t g;
    nb_gaze_init(&g, seed);
    nb_gaze_set_mood(&g, valence, arousal);
    stats_t s = {0};
    int prev[2] = {g.last[0], g.last[1]};
    nb_avatar_look_t before = nb_gaze_look(&g);
    while (s.looks < n) {
        const uint32_t elapsed_before = g.elapsed_ms;
        nb_gaze_tick(&g, 20);
        const nb_avatar_look_t now = nb_gaze_look(&g);
        const float step = fmaxf(fabsf(now.eye_x - before.eye_x), fabsf(now.eye_y - before.eye_y));
        s.max_step = fmaxf(s.max_step, step);
        before = now;
        if (fabsf(now.yaw) > 27.01f || fabsf(now.pitch) > 26.01f || fabsf(now.eye_y) > 82.01f) {
            s.out_of_range = true;
        }
        if (g.elapsed_ms >= elapsed_before) {
            continue;  // same look
        }
        const int k = g.last[0];
        s.repeated |= k == prev[0] || k == prev[1];
        prev[1] = prev[0];
        prev[0] = k;
        s.looks++;
        s.hold_ms += g.hold_ms;
        s.reach += sqrt((double)g.to.yaw * g.to.yaw / (27.0 * 27.0) +
                        (double)g.to.pitch * g.to.pitch / (26.0 * 26.0));
        if (k == NB_GAZE_CENTER) {
            s.center++;
        } else if (g.to.eye_y < -1.0f) {
            s.up++;
        } else if (g.to.eye_y > 1.0f) {
            s.down++;
        }
    }
    s.hold_ms /= n;
    s.reach /= n;
    return s;
}

static void test_looks_never_repeat_and_stay_in_range(void) {
    const float moods[][2] = {{0, 0}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
    for (size_t i = 0; i < sizeof(moods) / sizeof(moods[0]); i++) {
        const stats_t s = run(7u + (uint32_t)i, moods[i][0], moods[i][1], 600);
        NB_CHECK(!s.repeated);
        NB_CHECK(!s.out_of_range);
        // The eyes travel, they never jump: even a fast sweep from up to
        // down moves at most a fortieth of the face per frame.
        NB_CHECK(s.max_step < 25.0f);
    }
}

static void test_about_one_look_in_four_goes_back_to_the_center(void) {
    const stats_t s = run(3u, 0.0f, 0.0f, 2000);
    const double share = (double)s.center / s.looks;
    NB_CHECK(share > 0.17 && share < 0.33);
}

static void test_arousal_sets_the_pace_and_reach(void) {
    const stats_t calm = run(11u, 0.0f, -1.0f, 800);
    const stats_t alert = run(11u, 0.0f, 1.0f, 800);
    NB_CHECK(calm.hold_ms > 4500.0 && calm.hold_ms < 6000.0);  // 3.5-7 s
    NB_CHECK(alert.hold_ms > 1600.0 && alert.hold_ms < 2600.0);  // 1.2-3 s
    NB_CHECK(alert.reach > calm.reach * 1.5);
    nb_gaze_t g;
    nb_gaze_init(&g, 1u);
    nb_gaze_set_mood(&g, 0.0f, -1.0f);
    NB_CHECK(nb_gaze_blink_pace(&g) > 1.4f);  // calm blinks slowly
    nb_gaze_set_mood(&g, 0.0f, 1.0f);
    NB_CHECK(nb_gaze_blink_pace(&g) < 0.9f);
}

static void test_valence_pulls_up_or_down(void) {
    const stats_t happy = run(5u, 1.0f, 0.0f, 1500);
    const stats_t neutral = run(5u, 0.0f, 0.0f, 1500);
    const stats_t sad = run(5u, -1.0f, 0.0f, 1500);
    NB_CHECK(happy.up > happy.down * 2);
    NB_CHECK(sad.down > sad.up * 2);
    // A sad robot looks straight back less often (the no-repeat rule keeps
    // some center looks: it cannot stare down twice in a row).
    NB_CHECK(sad.center * 3 < neutral.center * 2);
}

static void test_same_seed_same_looks(void) {
    nb_gaze_t a, b;
    nb_gaze_init(&a, 42u);
    nb_gaze_init(&b, 42u);
    nb_gaze_set_mood(&a, 0.3f, -0.2f);
    nb_gaze_set_mood(&b, 0.3f, -0.2f);
    for (int i = 0; i < 5000; i++) {
        nb_gaze_tick(&a, 33);
        nb_gaze_tick(&b, 33);
    }
    const nb_avatar_look_t la = nb_gaze_look(&a), lb = nb_gaze_look(&b);
    NB_CHECK(la.yaw == lb.yaw && la.eye_y == lb.eye_y);
}

static void test_a_long_tick_moves_on(void) {
    nb_gaze_t g;
    nb_gaze_init(&g, 2u);
    nb_gaze_tick(&g, 60000);  // the display stalled for a minute
    NB_CHECK(g.elapsed_ms < g.move_ms + g.hold_ms);
    const int before = g.last[0];
    nb_gaze_tick(&g, g.move_ms + g.hold_ms);
    NB_CHECK(g.last[0] != before);
}

int main(void) {
    NB_RUN(test_looks_never_repeat_and_stay_in_range);
    NB_RUN(test_about_one_look_in_four_goes_back_to_the_center);
    NB_RUN(test_arousal_sets_the_pace_and_reach);
    NB_RUN(test_valence_pulls_up_or_down);
    NB_RUN(test_same_seed_same_looks);
    NB_RUN(test_a_long_tick_moves_on);
    NB_TEST_MAIN_END();
}
