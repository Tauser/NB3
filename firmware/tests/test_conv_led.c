#include "nb_conv_led.h"
#include "nb_test.h"

#define HOLD 1500

static void test_rest_shows_no_conversation(void) {
    nb_conv_led_t led;
    nb_conv_led_init(&led, HOLD);
    NB_CHECK_EQ_INT(NB_CONV_REST, nb_conv_led_step(&led, NB_CONV_REST, 0));
    NB_CHECK_EQ_INT(NB_CONV_REST, nb_conv_led_step(&led, NB_CONV_REST, 100000));
}

static void test_wake_answers_at_once(void) {
    nb_conv_led_t led;
    nb_conv_led_init(&led, HOLD);
    NB_CHECK_EQ_INT(NB_CONV_WAITING, nb_conv_led_step(&led, NB_CONV_WAITING, 5000));
}

static void test_change_waits_for_hold(void) {
    nb_conv_led_t led;
    nb_conv_led_init(&led, HOLD);
    nb_conv_led_step(&led, NB_CONV_LISTENING, 0);
    NB_CHECK_EQ_INT(NB_CONV_LISTENING, nb_conv_led_step(&led, NB_CONV_SPEAKING, 100));
    NB_CHECK_EQ_INT(NB_CONV_LISTENING, nb_conv_led_step(&led, NB_CONV_SPEAKING, 1599));
    NB_CHECK_EQ_INT(NB_CONV_SPEAKING, nb_conv_led_step(&led, NB_CONV_SPEAKING, 1600));
}

static void test_flicker_never_shows(void) {
    nb_conv_led_t led;
    nb_conv_led_init(&led, HOLD);
    nb_conv_led_step(&led, NB_CONV_LISTENING, 0);
    // Waiting and listening alternating every second, as the voice detector does.
    for (uint32_t t = 1000; t < 30000; t += 1000) {
        nb_conv_state_t raw = (t / 1000) % 2 ? NB_CONV_WAITING : NB_CONV_LISTENING;
        NB_CHECK_EQ_INT(NB_CONV_LISTENING, nb_conv_led_step(&led, raw, t));
    }
}

static void test_return_to_rest_after_hold(void) {
    nb_conv_led_t led;
    nb_conv_led_init(&led, HOLD);
    nb_conv_led_step(&led, NB_CONV_SPEAKING, 0);
    NB_CHECK_EQ_INT(NB_CONV_SPEAKING, nb_conv_led_step(&led, NB_CONV_REST, 10));
    NB_CHECK_EQ_INT(NB_CONV_REST, nb_conv_led_step(&led, NB_CONV_REST, 1510));
}

static void test_clock_wraparound(void) {
    nb_conv_led_t led;
    nb_conv_led_init(&led, HOLD);
    nb_conv_led_step(&led, NB_CONV_LISTENING, 0xFFFFFF00u);
    nb_conv_led_step(&led, NB_CONV_SPEAKING, 0xFFFFFF10u);
    NB_CHECK_EQ_INT(NB_CONV_SPEAKING, nb_conv_led_step(&led, NB_CONV_SPEAKING, 0x700u));
}

static void test_colors(void) {
    nb_rgb_t l = nb_conv_led_color(NB_CONV_LISTENING, 255);
    NB_CHECK(l.g == 255 && l.r == 0 && l.b == 0);
    nb_rgb_t s = nb_conv_led_color(NB_CONV_SPEAKING, 255);
    NB_CHECK(s.b == 255 && s.r == 0);
    nb_rgb_t half = nb_conv_led_color(NB_CONV_LISTENING, 128);
    NB_CHECK(half.g == 128);
    nb_rgb_t rest = nb_conv_led_color(NB_CONV_REST, 255);
    NB_CHECK(rest.r == 0 && rest.g == 0 && rest.b == 0);
}

int main(void) {
    NB_RUN(test_rest_shows_no_conversation);
    NB_RUN(test_wake_answers_at_once);
    NB_RUN(test_change_waits_for_hold);
    NB_RUN(test_flicker_never_shows);
    NB_RUN(test_return_to_rest_after_hold);
    NB_RUN(test_clock_wraparound);
    NB_RUN(test_colors);
    NB_TEST_MAIN_END();
}
