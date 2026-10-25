#include <math.h>

#include "nb_face_anim.h"
#include "nb_test.h"

#define FRAME_MS 33u

static int near(float a, float b, float tol) {
    return fabsf(a - b) <= tol;
}

static void test_eases_to_target_without_overshoot(void) {
    nb_face_anim_t a;
    nb_face_params_t neutral = nb_expression_params(NB_EXPR_NEUTRAL);
    nb_face_params_t happy = nb_expression_params(NB_EXPR_HAPPY);
    nb_face_anim_init(&a, neutral, 1, 0);
    nb_face_anim_set_target(&a, happy);
    float prev = a.current.mouth_curve;
    for (uint32_t t = FRAME_MS; t <= 1000; t += FRAME_MS) {
        nb_face_anim_step(&a, t);
        NB_CHECK(a.current.mouth_curve >= prev);
        NB_CHECK(a.current.mouth_curve <= happy.mouth_curve + 1e-6f);
        prev = a.current.mouth_curve;
    }
    NB_CHECK(near(a.current.mouth_curve, happy.mouth_curve, 0.01f));
    NB_CHECK(near(a.current.blush, happy.blush, 0.01f));
}

static void test_easing_is_gradual(void) {
    nb_face_anim_t a;
    nb_face_anim_init(&a, nb_expression_params(NB_EXPR_NEUTRAL), 1, 0);
    nb_face_anim_set_target(&a, nb_expression_params(NB_EXPR_SAD));
    nb_face_anim_step(&a, FRAME_MS);
    // One frame moves part of the way, not all of it.
    float sad = nb_expression_params(NB_EXPR_SAD).mouth_curve;
    float neutral = nb_expression_params(NB_EXPR_NEUTRAL).mouth_curve;
    NB_CHECK(a.current.mouth_curve < neutral && a.current.mouth_curve > sad);
}

static void test_blinks_within_max_interval_and_reopens(void) {
    nb_face_anim_t a;
    nb_face_anim_init(&a, nb_expression_params(NB_EXPR_NEUTRAL), 42, 0);
    float min_open = 1.0f;
    uint32_t first_close = 0;
    for (uint32_t t = 0; t <= NB_FACE_ANIM_BLINK_MAX_MS + NB_FACE_ANIM_BLINK_MS; t += 10) {
        nb_face_params_t p = nb_face_anim_step(&a, t);
        if (p.eye_open < min_open) {
            min_open = p.eye_open;
        }
        if (first_close == 0 && p.eye_open < 0.5f) {
            first_close = t;
        }
    }
    NB_CHECK(min_open < 0.15f);
    NB_CHECK(first_close >= NB_FACE_ANIM_BLINK_MIN_MS);
    nb_face_params_t after = nb_face_anim_step(&a, a.last_ms + 10);
    NB_CHECK(near(after.eye_open, 1.0f, 0.01f));
}

static void test_blink_does_not_change_eased_pose(void) {
    nb_face_anim_t a;
    nb_face_anim_init(&a, nb_expression_params(NB_EXPR_NEUTRAL), 7, 0);
    for (uint32_t t = 0; t <= NB_FACE_ANIM_BLINK_MAX_MS + 500; t += 10) {
        nb_face_anim_step(&a, t);
        NB_CHECK(near(a.current.eye_open, 1.0f, 1e-6f));
    }
}

static void test_same_seed_same_blinks(void) {
    nb_face_anim_t a, b;
    nb_face_params_t n = nb_expression_params(NB_EXPR_NEUTRAL);
    nb_face_anim_init(&a, n, 123, 0);
    nb_face_anim_init(&b, n, 123, 0);
    NB_CHECK_EQ_INT(a.next_blink_ms, b.next_blink_ms);
    nb_face_anim_t c;
    nb_face_anim_init(&c, n, 0, 0);  // zero seed must still produce blinks
    NB_CHECK(c.next_blink_ms >= NB_FACE_ANIM_BLINK_MIN_MS);
    NB_CHECK(c.next_blink_ms <= NB_FACE_ANIM_BLINK_MAX_MS);
}

static void test_speaking_moves_mouth_and_stops(void) {
    nb_face_anim_t a;
    nb_face_anim_init(&a, nb_expression_params(NB_EXPR_NEUTRAL), 1, 0);
    nb_face_anim_set_speaking(&a, true);
    float lo = 1.0f, hi = 0.0f;
    for (uint32_t t = FRAME_MS; t <= 1000; t += FRAME_MS) {
        nb_face_params_t p = nb_face_anim_step(&a, t);
        lo = p.mouth_open < lo ? p.mouth_open : lo;
        hi = p.mouth_open > hi ? p.mouth_open : hi;
    }
    NB_CHECK(hi > 0.4f && lo < 0.3f);
    nb_face_anim_set_speaking(&a, false);
    nb_face_params_t p = nb_face_anim_step(&a, 1033);
    NB_CHECK(near(p.mouth_open, nb_expression_params(NB_EXPR_NEUTRAL).mouth_open, 0.01f));
}

static void test_survives_clock_wraparound(void) {
    nb_face_anim_t a;
    nb_face_anim_init(&a, nb_expression_params(NB_EXPR_NEUTRAL), 5, UINT32_MAX - 50);
    nb_face_anim_set_target(&a, nb_expression_params(NB_EXPR_HAPPY));
    nb_face_anim_step(&a, 50);  // 101 ms later across the wrap
    NB_CHECK(a.current.mouth_curve > nb_expression_params(NB_EXPR_NEUTRAL).mouth_curve);
    NB_CHECK(a.current.mouth_curve < nb_expression_params(NB_EXPR_HAPPY).mouth_curve);
}

int main(void) {
    NB_RUN(test_eases_to_target_without_overshoot);
    NB_RUN(test_easing_is_gradual);
    NB_RUN(test_blinks_within_max_interval_and_reopens);
    NB_RUN(test_blink_does_not_change_eased_pose);
    NB_RUN(test_same_seed_same_blinks);
    NB_RUN(test_speaking_moves_mouth_and_stops);
    NB_RUN(test_survives_clock_wraparound);
    NB_TEST_MAIN_END();
}
