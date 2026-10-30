#include "nb_barge_gate.h"
#include "nb_test.h"

static nb_barge_gate_t armed_gate(void) {
    const nb_barge_gate_config_t cfg = {.min_speech_frames = 3, .hold_frames = 2};
    nb_barge_gate_t g;
    nb_barge_gate_init(&g, &cfg);
    nb_barge_gate_arm(&g, true);
    return g;
}

static void test_disarmed_passes_everything(void) {
    const nb_barge_gate_config_t cfg = {.min_speech_frames = 3, .hold_frames = 2};
    nb_barge_gate_t g;
    nb_barge_gate_init(&g, &cfg);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_PASS);
}

static void test_noise_shorter_than_min_speech_is_held(void) {
    nb_barge_gate_t g = armed_gate();
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
}

static void test_sustained_speech_opens(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true);
    nb_barge_gate_step(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_OPENED);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_PASS);
}

static void test_short_pause_keeps_it_open(void) {
    nb_barge_gate_t g = armed_gate();
    for (int i = 0; i < 3; i++) {
        nb_barge_gate_step(&g, true);
    }
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_PASS);
}

static void test_long_silence_closes_and_needs_speech_again(void) {
    nb_barge_gate_t g = armed_gate();
    for (int i = 0; i < 3; i++) {
        nb_barge_gate_step(&g, true);
    }
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_OPENED);
}

static void test_rearming_forgets_counted_speech(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true);
    nb_barge_gate_step(&g, true);
    nb_barge_gate_arm(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_HOLD);
}

static void test_disarm_while_open_passes(void) {
    nb_barge_gate_t g = armed_gate();
    for (int i = 0; i < 3; i++) {
        nb_barge_gate_step(&g, true);
    }
    nb_barge_gate_arm(&g, false);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_PASS);
    nb_barge_gate_arm(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_HOLD);
}

static void test_zero_config_is_clamped(void) {
    const nb_barge_gate_config_t cfg = {0};
    nb_barge_gate_t g;
    nb_barge_gate_init(&g, &cfg);
    nb_barge_gate_arm(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, true) == NB_BARGE_OPENED);
    NB_CHECK(nb_barge_gate_step(&g, false) == NB_BARGE_HOLD);
}

int main(void) {
    NB_RUN(test_disarmed_passes_everything);
    NB_RUN(test_noise_shorter_than_min_speech_is_held);
    NB_RUN(test_sustained_speech_opens);
    NB_RUN(test_short_pause_keeps_it_open);
    NB_RUN(test_long_silence_closes_and_needs_speech_again);
    NB_RUN(test_rearming_forgets_counted_speech);
    NB_RUN(test_disarm_while_open_passes);
    NB_RUN(test_zero_config_is_clamped);
    NB_TEST_MAIN_END();
}
