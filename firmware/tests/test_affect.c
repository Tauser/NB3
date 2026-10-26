#include <math.h>

#include "nb_affect.h"
#include "nb_test.h"

#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static nb_affect_stimulus_t stim(const char *key, float valence, float arousal, float cap) {
    nb_affect_stimulus_t s = {key, valence, arousal, cap};
    return s;
}

static void tick_for(nb_affect_t *a, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 100u) {
        nb_affect_tick(a, 100u);
    }
}

static void test_starts_and_stays_at_temperament(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    NB_CHECK(NEAR(a.mood.valence, 0.10f, 1e-6f));
    NB_CHECK(NEAR(a.mood.arousal, 0.05f, 1e-6f));
    tick_for(&a, 60000u);
    NB_CHECK(NEAR(a.mood.valence, 0.10f, 1e-5f));
    NB_CHECK(NEAR(a.mood.arousal, 0.05f, 1e-5f));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_affect_resolve(&a));
}

static void test_positive_deviation_falls_to_5_percent_in_recovery(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t s = stim("test:up", 0.5f, 0.5f, 2.0f);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, 0), 1.0f, 1e-6f));
    tick_for(&a, 60000u);
    NB_CHECK(NEAR(a.mood.valence - 0.10f, 0.025f, 0.002f));
    NB_CHECK(NEAR(a.mood.arousal - 0.05f, 0.025f, 0.002f));
}

static void test_negative_valence_recovers_slower(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t s = stim("test:down", -0.6f, 0.0f, 2.0f);
    nb_affect_stimulate(&a, &s, 0);
    tick_for(&a, 60000u);
    // Still clearly below the temperament after one recovery period...
    NB_CHECK(a.mood.valence < 0.10f - 0.05f);
    // ...and back near it after a few more.
    tick_for(&a, 180000u);
    NB_CHECK(NEAR(a.mood.valence, 0.10f, 0.01f));
}

static void test_repeated_stimulus_habituates_and_is_capped(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t s = stim("xiaozhi:happy", 0.2f, 0.1f, 0.55f);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, 0), 1.0f, 1e-6f));
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, 1000), 0.5f, 1e-6f));
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, 2000), 0.25f, 1e-6f));
    for (uint32_t i = 3; i < 50; i++) {
        nb_affect_stimulate(&a, &s, i * 100u);
    }
    // 0.3 per push halving each time would sum to 0.6; the cap stops it at 0.55.
    const float moved = (a.mood.valence - 0.10f) + (a.mood.arousal - 0.05f);
    NB_CHECK(moved <= 0.55f + 1e-5f);
    NB_CHECK(moved > 0.55f - 0.01f);
}

static void test_cap_limits_a_strong_first_push(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t s = stim("mcp:praise", 0.8f, 0.0f, 0.2f);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, 0), 0.25f, 1e-6f));
    NB_CHECK(NEAR(a.mood.valence, 0.30f, 1e-5f));
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, 10), 0.0f, 1e-6f));
}

static void test_habituation_window_resets(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t s = stim("touch:tap", 0.1f, 0.0f, 1.0f);
    nb_affect_stimulate(&a, &s, 0);
    nb_affect_stimulate(&a, &s, 100);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, NB_AFFECT_HABIT_WINDOW_MS + 200u), 1.0f, 1e-6f));
}

static void test_keys_habituate_independently(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t tap = stim("touch:tap", 0.1f, 0.0f, 1.0f);
    nb_affect_stimulus_t happy = stim("xiaozhi:happy", 0.1f, 0.0f, 1.0f);
    nb_affect_stimulate(&a, &tap, 0);
    nb_affect_stimulate(&a, &tap, 10);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &happy, 20), 1.0f, 1e-6f));
}

static void test_full_habit_table_recycles_oldest_window(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    char keys[NB_AFFECT_HABIT_SLOTS + 1][8];
    for (int i = 0; i <= NB_AFFECT_HABIT_SLOTS; i++) {
        keys[i][0] = 'k';
        keys[i][1] = (char)('a' + i);
        keys[i][2] = '\0';
        nb_affect_stimulus_t s = stim(keys[i], 0.001f, 0.0f, 1.0f);
        NB_CHECK(NEAR(nb_affect_stimulate(&a, &s, (uint32_t)i), 1.0f, 1e-6f));
    }
    // "ka" was evicted by the 17th key, so it starts fresh.
    nb_affect_stimulus_t first = stim(keys[0], 0.001f, 0.0f, 1.0f);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &first, 100), 1.0f, 1e-6f));
    // The most recent key is still habituated.
    nb_affect_stimulus_t last = stim(keys[NB_AFFECT_HABIT_SLOTS], 0.001f, 0.0f, 1.0f);
    NB_CHECK(NEAR(nb_affect_stimulate(&a, &last, 101), 0.5f, 1e-6f));
}

static void test_reactivity_scales_stimuli(void) {
    nb_affect_personality_t p = nb_affect_default_personality();
    p.reactivity = 0.5f;
    nb_affect_t a;
    nb_affect_init(&a, &p);
    nb_affect_stimulus_t s = stim("test:up", 0.4f, 0.0f, 2.0f);
    nb_affect_stimulate(&a, &s, 0);
    NB_CHECK(NEAR(a.mood.valence, 0.30f, 1e-5f));
}

static void test_bias_moves_rest_point_without_accumulating(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    for (int i = 0; i < 100; i++) {
        nb_affect_set_bias(&a, 0.5f, -0.5f);
    }
    NB_CHECK(NEAR(a.bias.valence, NB_AFFECT_BIAS_LIMIT, 1e-6f));
    NB_CHECK(NEAR(a.bias.arousal, -NB_AFFECT_BIAS_LIMIT, 1e-6f));
    tick_for(&a, 300000u);
    NB_CHECK(NEAR(a.mood.valence, 0.10f + NB_AFFECT_BIAS_LIMIT, 0.005f));
    NB_CHECK(NEAR(a.mood.arousal, 0.05f - NB_AFFECT_BIAS_LIMIT, 0.005f));
    // The bias alone stays inside the neutral region.
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_affect_resolve(&a));
}

static nb_expression_t resolve_at(float valence, float arousal) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    a.mood.valence = valence;
    a.mood.arousal = arousal;
    return nb_affect_resolve(&a);
}

static void test_regions_map_to_expressions(void) {
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, resolve_at(0.6f, 0.4f));
    NB_CHECK_EQ_INT(NB_EXPR_LOVING, resolve_at(0.6f, -0.4f));
    NB_CHECK_EQ_INT(NB_EXPR_SAD, resolve_at(-0.6f, -0.4f));
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, resolve_at(-0.6f, 0.5f));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, resolve_at(0.2f, 0.1f));
}

static void test_hysteresis_keeps_expression_on_the_border(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    // 0.26 from the temperament: too little to show from neutral...
    a.mood.valence = 0.10f + 0.26f;
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_affect_resolve(&a));
    // ...but once shown it stays until the mood is back under 0.22.
    a.mood.valence = 0.10f + 0.35f;
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_affect_resolve(&a));
    a.mood.valence = 0.10f + 0.26f;
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_affect_resolve(&a));
    a.mood.valence = 0.10f + 0.20f;
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_affect_resolve(&a));
}

static void test_hysteresis_between_regions(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    a.mood.valence = 0.6f;
    a.mood.arousal = 0.4f;
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_affect_resolve(&a));
    // Just past the HAPPY/LOVING midline: not enough to switch.
    a.mood.arousal = -0.02f;
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_affect_resolve(&a));
    a.mood.arousal = -0.3f;
    NB_CHECK_EQ_INT(NB_EXPR_LOVING, nb_affect_resolve(&a));
}

static void test_invalid_input_is_ignored(void) {
    nb_affect_t a;
    nb_affect_init(&a, NULL);
    nb_affect_stimulus_t no_key = stim(NULL, 1.0f, 1.0f, 1.0f);
    nb_affect_stimulus_t no_cap = stim("k", 1.0f, 1.0f, 0.0f);
    NB_CHECK(nb_affect_stimulate(&a, &no_key, 0) == 0.0f);
    NB_CHECK(nb_affect_stimulate(&a, &no_cap, 0) == 0.0f);
    NB_CHECK(nb_affect_stimulate(&a, NULL, 0) == 0.0f);
    NB_CHECK(nb_affect_stimulate(NULL, &no_cap, 0) == 0.0f);
    NB_CHECK(NEAR(a.mood.valence, 0.10f, 1e-6f));
    nb_affect_init(NULL, NULL);
    nb_affect_tick(NULL, 10);
    nb_affect_set_bias(NULL, 0, 0);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_affect_resolve(NULL));
}

int main(void) {
    NB_RUN(test_starts_and_stays_at_temperament);
    NB_RUN(test_positive_deviation_falls_to_5_percent_in_recovery);
    NB_RUN(test_negative_valence_recovers_slower);
    NB_RUN(test_repeated_stimulus_habituates_and_is_capped);
    NB_RUN(test_cap_limits_a_strong_first_push);
    NB_RUN(test_habituation_window_resets);
    NB_RUN(test_keys_habituate_independently);
    NB_RUN(test_full_habit_table_recycles_oldest_window);
    NB_RUN(test_reactivity_scales_stimuli);
    NB_RUN(test_bias_moves_rest_point_without_accumulating);
    NB_RUN(test_regions_map_to_expressions);
    NB_RUN(test_hysteresis_keeps_expression_on_the_border);
    NB_RUN(test_hysteresis_between_regions);
    NB_RUN(test_invalid_input_is_ignored);
    NB_TEST_MAIN_END();
}
