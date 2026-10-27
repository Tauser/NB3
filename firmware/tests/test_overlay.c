#include <math.h>

#include "nb_overlay.h"
#include "nb_test.h"

static nb_overlay_frame_t step(nb_overlay_t *o, nb_expression_t winner, bool speaking,
                               float level, uint32_t now) {
    nb_overlay_frame_t f;
    nb_overlay_update(o, winner, speaking, level, now, &f);
    return f;
}

static void test_each_state_gets_its_overlay(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    NB_CHECK_EQ_INT(NB_OVERLAY_NONE, step(&o, NB_EXPR_NEUTRAL, false, 0, 0).kind);
    NB_CHECK_EQ_INT(NB_OVERLAY_LISTEN, step(&o, NB_EXPR_LISTENING, false, 0, 10).kind);
    NB_CHECK_EQ_INT(NB_OVERLAY_THINK, step(&o, NB_EXPR_THINKING, false, 0, 20).kind);
    NB_CHECK_EQ_INT(NB_OVERLAY_AFFECTION, step(&o, NB_EXPR_LOVING, false, 0, 30).kind);
    NB_CHECK_EQ_INT(NB_OVERLAY_CHEEKS, step(&o, NB_EXPR_EMBARRASSED, false, 0, 40).kind);
    NB_CHECK_EQ_INT(NB_OVERLAY_NONE, step(&o, NB_EXPR_HAPPY, false, 0, 50).kind);
}

static void test_speaking_wins_over_the_expression(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    nb_overlay_frame_t f = step(&o, NB_EXPR_LOVING, true, 0.5f, 0);
    NB_CHECK_EQ_INT(NB_OVERLAY_SPEECH, f.kind);
    NB_CHECK(!f.cheeks);
}

static void test_bubble_pops_then_settles(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    step(&o, NB_EXPR_NEUTRAL, false, 0, 0);
    NB_CHECK(step(&o, NB_EXPR_NEUTRAL, true, 0, 10).bubble < 0.1f);
    NB_CHECK(step(&o, NB_EXPR_NEUTRAL, true, 0, 190).bubble > 1.05f);
    NB_CHECK(fabsf(step(&o, NB_EXPR_NEUTRAL, true, 0, 600).bubble - 1.0f) < 1e-4f);
}

static void test_bubble_lingers_then_leaves(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    step(&o, NB_EXPR_NEUTRAL, true, 0, 0);
    step(&o, NB_EXPR_NEUTRAL, true, 0, 1000);
    nb_overlay_frame_t f = step(&o, NB_EXPR_NEUTRAL, false, 0, 1000);
    NB_CHECK_EQ_INT(NB_OVERLAY_SPEECH, f.kind);
    f = step(&o, NB_EXPR_NEUTRAL, false, 0, 1250);
    NB_CHECK_EQ_INT(NB_OVERLAY_SPEECH, f.kind);
    NB_CHECK(f.bubble > 0.1f && f.bubble < 0.9f);
    NB_CHECK_EQ_INT(NB_OVERLAY_NONE, step(&o, NB_EXPR_NEUTRAL, false, 0, 1500).kind);
    // Listening right after the reply takes over once the bubble is gone.
    NB_CHECK_EQ_INT(NB_OVERLAY_LISTEN, step(&o, NB_EXPR_LISTENING, false, 0, 1600).kind);
}

static void test_voice_back_during_linger_keeps_the_bubble(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    step(&o, NB_EXPR_NEUTRAL, true, 0, 0);
    step(&o, NB_EXPR_NEUTRAL, false, 0, 1000);
    nb_overlay_frame_t f = step(&o, NB_EXPR_NEUTRAL, true, 0, 1200);
    NB_CHECK_EQ_INT(NB_OVERLAY_SPEECH, f.kind);
    NB_CHECK(fabsf(f.bubble - 1.0f) < 1e-4f);
    NB_CHECK_EQ_INT(NB_OVERLAY_SPEECH, step(&o, NB_EXPR_NEUTRAL, true, 0, 2500).kind);
}

static void test_hand_waits_then_slides_in(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    step(&o, NB_EXPR_THINKING, false, 0, 0);
    NB_CHECK(step(&o, NB_EXPR_THINKING, false, 0, 900).hand == 0.0f);
    const float half = step(&o, NB_EXPR_THINKING, false, 0, 1125).hand;
    NB_CHECK(half > 0.2f && half < 0.8f);
    nb_overlay_frame_t f = step(&o, NB_EXPR_THINKING, false, 0, 2000);
    NB_CHECK(f.hand == 1.0f);
    NB_CHECK(f.hand_angle <= 0.0f && f.hand_angle >= -7.0f);
    // A new thinking spell starts the wait again.
    step(&o, NB_EXPR_LISTENING, false, 0, 2100);
    step(&o, NB_EXPR_THINKING, false, 0, 2200);
    NB_CHECK(step(&o, NB_EXPR_THINKING, false, 0, 2500).hand == 0.0f);
}

static void test_thought_dots_take_turns(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    step(&o, NB_EXPR_THINKING, false, 0, 0);
    nb_overlay_frame_t f = step(&o, NB_EXPR_THINKING, false, 0, 300);
    NB_CHECK(f.dots[0] > f.dots[2]);
    for (int i = 0; i < 3; ++i) {
        NB_CHECK(f.dots[i] >= 0.25f && f.dots[i] <= 1.0f);
    }
}

static void test_bars_follow_the_level_irregularly(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    nb_overlay_frame_t quiet = step(&o, NB_EXPR_LISTENING, false, 0.0f, 0);
    for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
        NB_CHECK(fabsf(quiet.bars[i] - 0.12f) < 1e-4f);  // floor while silent
    }
    nb_overlay_frame_t loud = quiet;
    for (uint32_t t = 0; t <= 400; t += 33) {
        loud = step(&o, NB_EXPR_LISTENING, false, 1.0f, t);
    }
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
        NB_CHECK(loud.bars[i] > 0.4f && loud.bars[i] <= 1.0f);
        lo = fminf(lo, loud.bars[i]);
        hi = fmaxf(hi, loud.bars[i]);
    }
    NB_CHECK(hi - lo > 0.1f);  // not a flat row
}

static void test_level_falls_slower_than_it_rises(void) {
    nb_overlay_t up, down;
    nb_overlay_init(&up);
    nb_overlay_init(&down);
    step(&up, NB_EXPR_LISTENING, false, 0.0f, 0);
    step(&up, NB_EXPR_LISTENING, false, 1.0f, 40);
    step(&down, NB_EXPR_LISTENING, false, 1.0f, 0);
    down.level = 1.0f;
    step(&down, NB_EXPR_LISTENING, false, 0.0f, 40);
    NB_CHECK(up.level > 1.0f - down.level);
}

static void test_hearts_take_turns_rising(void) {
    nb_overlay_t o;
    nb_overlay_init(&o);
    step(&o, NB_EXPR_LOVING, false, 0, 0);
    nb_overlay_frame_t f = step(&o, NB_EXPR_LOVING, false, 0, 600);
    NB_CHECK(f.cheeks);
    NB_CHECK(fabsf(f.hearts[0] - 0.25f) < 1e-4f);
    NB_CHECK(fabsf(f.hearts[1] - 0.75f) < 1e-4f);
}

static void test_null_is_safe(void) {
    nb_overlay_frame_t f;
    nb_overlay_init(NULL);
    nb_overlay_update(NULL, NB_EXPR_NEUTRAL, false, 0, 0, &f);
    nb_overlay_t o;
    nb_overlay_init(&o);
    nb_overlay_update(&o, NB_EXPR_NEUTRAL, false, 0, 0, NULL);
}

static void test_sleep_shows_rising_zs(void)
{
    nb_overlay_t o;
    nb_overlay_frame_t f;
    nb_overlay_init(&o);
    nb_overlay_set_sleeping(&o, true);
    nb_overlay_update(&o, NB_EXPR_SLEEPY, false, 0.0f, 1000, &f);
    NB_CHECK_EQ_INT(NB_OVERLAY_SLEEP, f.kind);
    NB_CHECK(f.zs[1] == 0.0f && f.zs[2] == 0.0f);  // they start one by one
    // Later all three are up, each at its own height, and they keep cycling.
    nb_overlay_update(&o, NB_EXPR_SLEEPY, false, 0.0f, 1000 + 2500, &f);
    NB_CHECK(f.zs[0] > f.zs[1] && f.zs[1] > f.zs[2] && f.zs[2] > 0.0f);
    for (int i = 0; i < NB_OVERLAY_ZS; ++i) {
        NB_CHECK(f.zs[i] >= 0.0f && f.zs[i] < 1.0f);
    }
    // The voice still wins (the wake word answer), and waking ends the Zs.
    nb_overlay_update(&o, NB_EXPR_SLEEPY, true, 0.5f, 4000, &f);
    NB_CHECK_EQ_INT(NB_OVERLAY_SPEECH, f.kind);
    nb_overlay_set_sleeping(&o, false);
    nb_overlay_update(&o, NB_EXPR_NEUTRAL, false, 0.0f, 5000, &f);  // the bubble lingers
    nb_overlay_update(&o, NB_EXPR_NEUTRAL, false, 0.0f, 6000, &f);
    NB_CHECK_EQ_INT(NB_OVERLAY_NONE, f.kind);
    // Drowsy but awake: no Zs.
    nb_overlay_update(&o, NB_EXPR_SLEEPY, false, 0.0f, 7000, &f);
    NB_CHECK_EQ_INT(NB_OVERLAY_NONE, f.kind);
}

int main(void) {
    NB_RUN(test_each_state_gets_its_overlay);
    NB_RUN(test_speaking_wins_over_the_expression);
    NB_RUN(test_bubble_pops_then_settles);
    NB_RUN(test_bubble_lingers_then_leaves);
    NB_RUN(test_voice_back_during_linger_keeps_the_bubble);
    NB_RUN(test_hand_waits_then_slides_in);
    NB_RUN(test_thought_dots_take_turns);
    NB_RUN(test_bars_follow_the_level_irregularly);
    NB_RUN(test_level_falls_slower_than_it_rises);
    NB_RUN(test_hearts_take_turns_rising);
    NB_RUN(test_null_is_safe);
    NB_RUN(test_sleep_shows_rising_zs);
    NB_TEST_MAIN_END();
}
