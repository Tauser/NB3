#include "nb_barge_gate.h"
#include "nb_test.h"

#define LOUD (-10)
#define ECHO (-28)
#define MIN_LEVEL (-24)

static const nb_barge_gate_config_t kCfg = {
    .min_speech_frames = 3, .hold_frames = 2, .hold_evidence_frames = 2, .min_level_dbfs = MIN_LEVEL,
    .hold_level_dbfs = -26};

static const nb_barge_gate_config_t kRealCfg = {
    .min_speech_frames = 3, .hold_frames = 17, .hold_evidence_frames = 3,
    .min_level_dbfs = MIN_LEVEL, .hold_level_dbfs = -25};
static const nb_barge_gate_config_t kPauseCfg = {
    .min_speech_frames = 3, .hold_frames = 4, .hold_evidence_frames = 3,
    .min_level_dbfs = MIN_LEVEL, .hold_level_dbfs = -26};

static nb_barge_gate_t armed_config(const nb_barge_gate_config_t *cfg) {
    nb_barge_gate_t g;
    nb_barge_gate_init(&g, cfg);
    nb_barge_gate_arm(&g, true);
    return g;
}
static nb_barge_gate_t armed_gate(void) { return armed_config(&kCfg); }

static void open_it(nb_barge_gate_t *g) {
    for (int i = 0; i < 3; i++) {
        nb_barge_gate_step(g, true, LOUD);
    }
}

static void test_disarmed_passes_everything(void) {
    nb_barge_gate_t g;
    nb_barge_gate_init(&g, &kCfg);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_PASS);
}

static void test_speech_shorter_than_min_is_held(void) {
    nb_barge_gate_t g = armed_gate();
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, false, LOUD) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
}

static void test_loud_sustained_speech_opens(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true, LOUD);
    nb_barge_gate_step(&g, true, LOUD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_OPENED);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_PASS);
}

static void test_quiet_speech_like_echo_never_opens(void) {
    nb_barge_gate_t g = armed_gate();
    for (int i = 0; i < 20; i++) {
        NB_CHECK(nb_barge_gate_step(&g, true, ECHO) == NB_BARGE_HOLD);
    }
    NB_CHECK_EQ_INT(ECHO, nb_barge_gate_run_average(&g));
}

static void test_average_rides_over_a_dip_between_syllables(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true, -15);
    nb_barge_gate_step(&g, true, -32);
    NB_CHECK(nb_barge_gate_step(&g, true, -15) == NB_BARGE_OPENED);  // avg -20
}

static void test_window_slides_until_it_is_loud_enough(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true, ECHO);
    nb_barge_gate_step(&g, true, ECHO);
    NB_CHECK(nb_barge_gate_step(&g, true, ECHO) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_OPENED);  // (-28 - 28 - 10) / 3 = -22
}

static void test_run_average_needs_a_full_run(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true, LOUD);
    NB_CHECK_EQ_INT(NB_BARGE_LEVEL_SILENCE, nb_barge_gate_run_average(&g));
    nb_barge_gate_step(&g, false, LOUD);
    NB_CHECK_EQ_INT(NB_BARGE_LEVEL_SILENCE, nb_barge_gate_run_average(&g));
}

static void test_short_pause_keeps_it_open(void) {
    nb_barge_gate_t g = armed_config(&kPauseCfg);
    open_it(&g);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true, -25) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
}

static void test_long_silence_closes_and_needs_speech_again(void) {
    nb_barge_gate_t g = armed_gate();
    open_it(&g);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_OPENED);
}

static void test_weak_vad_speech_cannot_keep_gate_open(void) {
    nb_barge_gate_t g = armed_gate();
    open_it(&g);
    NB_CHECK(nb_barge_gate_step(&g, true, ECHO) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true, ECHO) == NB_BARGE_HOLD);
    for (int i = 0; i < 100; i++) {
        NB_CHECK(nb_barge_gate_step(&g, true, ECHO) == NB_BARGE_HOLD);
    }
    NB_CHECK(!g.open);
}

static void test_level_hysteresis_and_short_dip(void) {
    nb_barge_gate_t g = armed_config(&kRealCfg);
    open_it(&g);
    NB_CHECK(nb_barge_gate_step(&g, true, -25) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true, -40) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true, -26) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, true, -40) == NB_BARGE_PASS);
    NB_CHECK(g.open);
}

static void test_isolated_loud_echo_cannot_hold_open(void) {
    nb_barge_gate_t g = armed_config(&kRealCfg);
    open_it(&g);
    for (int i = 0; i < 80 && g.open; i++) {
        nb_barge_gate_step(&g, true, i % 16 == 15 ? -20 : -35);
    }
    NB_CHECK(!g.open);
}

static void test_sustained_speech_stays_open(void) {
    nb_barge_gate_t g = armed_config(&kRealCfg);
    open_it(&g);
    NB_CHECK_EQ_INT(LOUD, g.opening_average);
    for (int i = 0; i < 150; i++) {
        NB_CHECK(nb_barge_gate_step(&g, true, -20) == NB_BARGE_PASS);
    }
    NB_CHECK(g.open);
}

static void test_two_frame_pulses_do_not_renew_hold(void) {
    nb_barge_gate_t g = armed_config(&kRealCfg);
    open_it(&g);
    for (int i = 0; i < 17; i++) {
        nb_barge_gate_step(&g, true, i % 8 < 2 ? -20 : -35);
    }
    NB_CHECK(!g.open);
}

static void test_fractional_negative_average_does_not_round_up(void) {
    nb_barge_gate_t g = armed_config(&kRealCfg);
    nb_barge_gate_step(&g, true, -25);
    nb_barge_gate_step(&g, true, -25);
    NB_CHECK(nb_barge_gate_step(&g, true, -23) == NB_BARGE_HOLD);
    NB_CHECK_EQ_INT(-25, nb_barge_gate_run_average(&g));
    NB_CHECK(nb_barge_gate_step(&g, true, -24) == NB_BARGE_OPENED);
}

static void test_rearming_forgets_counted_speech(void) {
    nb_barge_gate_t g = armed_gate();
    nb_barge_gate_step(&g, true, LOUD);
    nb_barge_gate_step(&g, true, LOUD);
    nb_barge_gate_arm(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
}

static void test_disarm_while_open_passes(void) {
    nb_barge_gate_t g = armed_gate();
    open_it(&g);
    nb_barge_gate_arm(&g, false);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
    nb_barge_gate_arm(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_HOLD);
}

static void test_config_is_clamped(void) {
    const nb_barge_gate_config_t zero = {.min_level_dbfs = MIN_LEVEL};
    nb_barge_gate_t g;
    nb_barge_gate_init(&g, &zero);
    nb_barge_gate_arm(&g, true);
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_OPENED);
    NB_CHECK(nb_barge_gate_step(&g, false, LOUD) == NB_BARGE_HOLD);

    const nb_barge_gate_config_t huge = {
        .min_speech_frames = 200, .hold_frames = 1, .min_level_dbfs = MIN_LEVEL};
    nb_barge_gate_init(&g, &huge);
    nb_barge_gate_arm(&g, true);
    for (int i = 0; i < NB_BARGE_MAX_RUN - 1; i++) {
        NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_HOLD);
    }
    NB_CHECK(nb_barge_gate_step(&g, true, LOUD) == NB_BARGE_OPENED);
}

static void test_hold_evidence_is_clamped_to_hold_duration(void) {
    const nb_barge_gate_config_t cfg = {
        .min_speech_frames = 3, .hold_frames = 3, .hold_evidence_frames = 9,
        .min_level_dbfs = MIN_LEVEL, .hold_level_dbfs = -25};
    nb_barge_gate_t g = armed_config(&cfg);
    NB_CHECK_EQ_INT(3, g.cfg.hold_evidence_frames);
    open_it(&g);
    for (int i = 0; i < 6; i++)
        NB_CHECK(nb_barge_gate_step(&g, true, -25) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_PASS);
    NB_CHECK(nb_barge_gate_step(&g, false, ECHO) == NB_BARGE_HOLD);
}

static void test_intermittent_vad_can_close_hold(void) {
    nb_barge_gate_t g = armed_config(&kRealCfg);
    open_it(&g);
    for (int i = 0; i < 17; i++)
        nb_barge_gate_step(&g, i % 3 != 2, -20);
    NB_CHECK(!g.open);
}

static void test_level_of_silence_and_full_scale(void) {
    int16_t zeros[8] = {0};
    NB_CHECK_EQ_INT(NB_BARGE_LEVEL_SILENCE, nb_barge_level_dbfs(zeros, 8));
    NB_CHECK_EQ_INT(NB_BARGE_LEVEL_SILENCE, nb_barge_level_dbfs(zeros, 0));
    int16_t full[4] = {-32768, -32768, -32768, -32768};
    NB_CHECK_EQ_INT(0, nb_barge_level_dbfs(full, 4));
}

static void test_level_halves_are_six_db_apart(void) {
    int16_t loud[4] = {16384, -16384, 16384, -16384};  // -6.02 dBFS
    int16_t quiet[4] = {8192, -8192, 8192, -8192};     // -12.04 dBFS
    int16_t faint[4] = {33, -33, 33, -33};             // -59.9 dBFS
    NB_CHECK_EQ_INT(-7, nb_barge_level_dbfs(loud, 4));
    NB_CHECK_EQ_INT(-13, nb_barge_level_dbfs(quiet, 4));
    NB_CHECK_EQ_INT(-60, nb_barge_level_dbfs(faint, 4));
}

int main(void) {
    NB_RUN(test_disarmed_passes_everything);
    NB_RUN(test_speech_shorter_than_min_is_held);
    NB_RUN(test_loud_sustained_speech_opens);
    NB_RUN(test_quiet_speech_like_echo_never_opens);
    NB_RUN(test_average_rides_over_a_dip_between_syllables);
    NB_RUN(test_window_slides_until_it_is_loud_enough);
    NB_RUN(test_run_average_needs_a_full_run);
    NB_RUN(test_short_pause_keeps_it_open);
    NB_RUN(test_long_silence_closes_and_needs_speech_again);
    NB_RUN(test_weak_vad_speech_cannot_keep_gate_open);
    NB_RUN(test_level_hysteresis_and_short_dip);
    NB_RUN(test_isolated_loud_echo_cannot_hold_open);
    NB_RUN(test_sustained_speech_stays_open);
    NB_RUN(test_two_frame_pulses_do_not_renew_hold);
    NB_RUN(test_fractional_negative_average_does_not_round_up);
    NB_RUN(test_rearming_forgets_counted_speech);
    NB_RUN(test_disarm_while_open_passes);
    NB_RUN(test_config_is_clamped);
    NB_RUN(test_hold_evidence_is_clamped_to_hold_duration);
    NB_RUN(test_intermittent_vad_can_close_hold);
    NB_RUN(test_level_of_silence_and_full_scale);
    NB_RUN(test_level_halves_are_six_db_apart);
    NB_TEST_MAIN_END();
}
