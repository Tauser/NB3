#include "nb_mood.h"
#include "nb_test.h"

#define FRAME_MS 30u

static int max_channel(nb_rgb_t c) {
    int m = c.r > c.g ? c.r : c.g;
    return m > c.b ? m : c.b;
}

static void test_output_never_exceeds_max_brightness(void) {
    for (int e = 0; e < NB_EXPR_COUNT; e++) {
        nb_mood_t m;
        nb_mood_init(&m, 40, 0);
        nb_mood_set_expression(&m, (nb_expression_t)e);
        for (uint32_t t = 0; t < 10000; t += FRAME_MS) {
            NB_CHECK(max_channel(nb_mood_step(&m, t)) <= 40);
        }
    }
}

static void test_steady_expression_settles_on_its_color(void) {
    nb_mood_t m;
    nb_mood_init(&m, 255, 0);
    nb_mood_set_expression(&m, NB_EXPR_ANGRY);  // pulsing red
    nb_mood_set_expression(&m, NB_EXPR_LISTENING);  // steady cyan
    nb_rgb_t c = {0, 0, 0};
    for (uint32_t t = FRAME_MS; t <= 3000; t += FRAME_MS) {
        c = nb_mood_step(&m, t);
    }
    NB_CHECK(c.r < 5 && c.g > 210 && c.b > 245);
}

static void test_color_change_is_gradual(void) {
    nb_mood_t m;
    nb_mood_init(&m, 255, 0);
    nb_mood_set_expression(&m, NB_EXPR_LISTENING);
    for (uint32_t t = FRAME_MS; t <= 3000; t += FRAME_MS) {
        nb_mood_step(&m, t);
    }
    nb_mood_set_expression(&m, NB_EXPR_SURPRISED);  // steady white
    nb_rgb_t c = nb_mood_step(&m, 3000 + FRAME_MS);
    NB_CHECK(c.r > 0 && c.r < 200);  // on its way from 0 to 255, not there yet
}

static void test_breathing_varies_within_its_floor(void) {
    nb_mood_t m;
    nb_mood_init(&m, 255, 0);
    nb_mood_set_expression(&m, NB_EXPR_SAD);
    int lo = 255, hi = 0;
    for (uint32_t t = 0; t < 20000; t += FRAME_MS) {
        int v = max_channel(nb_mood_step(&m, t));
        if (t > 2000) {  // after the color has settled
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
    }
    NB_CHECK(hi > 180);
    NB_CHECK(lo < 80);
    NB_CHECK(lo > 0);  // the floor keeps it from going dark
}

static void test_dizzy_cycles_hues(void) {
    nb_mood_t m;
    nb_mood_init(&m, 255, 0);
    nb_mood_set_expression(&m, NB_EXPR_DIZZY);
    int red_dominant = 0, green_dominant = 0, blue_dominant = 0;
    for (uint32_t t = 0; t < 6000; t += FRAME_MS) {
        nb_rgb_t c = nb_mood_step(&m, t);
        if (c.r > c.g && c.r > c.b) red_dominant++;
        if (c.g > c.r && c.g > c.b) green_dominant++;
        if (c.b > c.r && c.b > c.g) blue_dominant++;
    }
    NB_CHECK(red_dominant > 0 && green_dominant > 0 && blue_dominant > 0);
}

static void test_expressions_have_distinct_colors(void) {
    nb_mood_t a, b;
    nb_mood_init(&a, 255, 0);
    nb_mood_init(&b, 255, 0);
    nb_mood_set_expression(&a, NB_EXPR_ANGRY);
    nb_mood_set_expression(&b, NB_EXPR_SAD);
    for (uint32_t t = FRAME_MS; t <= 3000; t += FRAME_MS) {
        nb_mood_step(&a, t);
        nb_mood_step(&b, t);
    }
    NB_CHECK(a.r > a.b);
    NB_CHECK(b.b > b.r);
}

static void test_invalid_expression_falls_back_to_neutral(void) {
    nb_mood_t m;
    nb_mood_init(&m, 255, 0);
    nb_mood_set_expression(&m, NB_EXPR_COUNT);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, m.expression);
}

int main(void) {
    NB_RUN(test_output_never_exceeds_max_brightness);
    NB_RUN(test_steady_expression_settles_on_its_color);
    NB_RUN(test_color_change_is_gradual);
    NB_RUN(test_breathing_varies_within_its_floor);
    NB_RUN(test_dizzy_cycles_hues);
    NB_RUN(test_expressions_have_distinct_colors);
    NB_RUN(test_invalid_expression_falls_back_to_neutral);
    NB_TEST_MAIN_END();
}
