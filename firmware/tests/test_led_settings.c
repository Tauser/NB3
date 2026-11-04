#include "nb_led_settings.h"
#include "nb_test.h"

static void test_default_is_on_with_the_mood_color(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 6u);
    NB_CHECK(s.on);
    NB_CHECK_EQ_INT(6, s.brightness);
    NB_CHECK(!s.fixed_color);
}

static void test_brightness_is_held_inside_its_range(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 6u);
    s.brightness = 255;
    NB_CHECK(nb_led_settings_clamp(&s));
    NB_CHECK_EQ_INT(NB_LED_BRIGHTNESS_MAX, s.brightness);
    s.brightness = 0;
    NB_CHECK(nb_led_settings_clamp(&s));
    NB_CHECK_EQ_INT(NB_LED_BRIGHTNESS_MIN, s.brightness);
    s.brightness = 40;
    NB_CHECK(!nb_led_settings_clamp(&s));
    NB_CHECK_EQ_INT(40, s.brightness);
}

static void test_a_default_above_the_ceiling_is_pulled_down(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 200u);
    NB_CHECK_EQ_INT(NB_LED_BRIGHTNESS_MAX, s.brightness);
}

static void test_off_is_black_whatever_the_color(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 50u);
    s.on = false;
    s.fixed_color = true;
    s.color.r = 255;
    const nb_rgb_t mood = {10, 20, 30};
    nb_rgb_t out = nb_led_settings_resolve(&s, mood);
    NB_CHECK(out.r == 0 && out.g == 0 && out.b == 0);
}

static void test_the_mood_color_passes_through_when_not_fixed(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 50u);
    const nb_rgb_t mood = {10, 20, 30};
    nb_rgb_t out = nb_led_settings_resolve(&s, mood);
    NB_CHECK(out.r == 10 && out.g == 20 && out.b == 30);
}

static void test_a_fixed_color_is_scaled_to_the_brightness(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 80u);
    s.fixed_color = true;
    s.color.r = 255;
    s.color.g = 0;
    s.color.b = 0;
    const nb_rgb_t mood = {1, 2, 3};
    nb_rgb_t out = nb_led_settings_resolve(&s, mood);
    NB_CHECK(out.r == 80 && out.g == 0 && out.b == 0);
    // Half-strength input keeps its hue: the strongest channel carries the brightness.
    s.color.r = 100;
    s.color.g = 50;
    out = nb_led_settings_resolve(&s, mood);
    NB_CHECK(out.r == 80 && out.g == 40 && out.b == 0);
}

static void test_no_fixed_color_ever_exceeds_the_ceiling(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 255u);
    s.fixed_color = true;
    s.color.r = 255;
    s.color.g = 255;
    s.color.b = 255;
    const nb_rgb_t mood = {0, 0, 0};
    nb_rgb_t out = nb_led_settings_resolve(&s, mood);
    NB_CHECK(out.r <= NB_LED_BRIGHTNESS_MAX && out.g <= NB_LED_BRIGHTNESS_MAX &&
             out.b <= NB_LED_BRIGHTNESS_MAX);
}

static void test_a_black_fixed_color_stays_black(void) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, 50u);
    s.fixed_color = true;
    s.color.r = s.color.g = s.color.b = 0;
    const nb_rgb_t mood = {9, 9, 9};
    nb_rgb_t out = nb_led_settings_resolve(&s, mood);
    NB_CHECK(out.r == 0 && out.g == 0 && out.b == 0);
}

static void test_null_calls_are_safe(void) {
    nb_led_settings_default(NULL, 6u);
    NB_CHECK(!nb_led_settings_clamp(NULL));
    const nb_rgb_t mood = {1, 2, 3};
    nb_rgb_t out = nb_led_settings_resolve(NULL, mood);
    NB_CHECK(out.r == 0 && out.g == 0 && out.b == 0);
}

int main(void) {
    NB_RUN(test_default_is_on_with_the_mood_color);
    NB_RUN(test_brightness_is_held_inside_its_range);
    NB_RUN(test_a_default_above_the_ceiling_is_pulled_down);
    NB_RUN(test_off_is_black_whatever_the_color);
    NB_RUN(test_the_mood_color_passes_through_when_not_fixed);
    NB_RUN(test_a_fixed_color_is_scaled_to_the_brightness);
    NB_RUN(test_no_fixed_color_ever_exceeds_the_ceiling);
    NB_RUN(test_a_black_fixed_color_stays_black);
    NB_RUN(test_null_calls_are_safe);
    NB_TEST_MAIN_END();
}
