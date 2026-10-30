#include "nb_test.h"
#include "nb_wake_detect.h"

// The Nexo model's input tensor: scale 26/255, zero point -128.
#define INPUT_SCALE 0.10196078568696976f
#define INPUT_ZP (-128)

static const nb_wake_detect_config_t kConfig = {
    .cutoff = 230, .window = 5, .warmup_steps = 3, .report_floor = 100};

static nb_wake_detect_t fresh(void) {
    nb_wake_detect_t d;
    nb_wake_detect_init(&d, &kConfig);
    return d;
}

static nb_wake_event_t feed(nb_wake_detect_t *d, uint8_t p, int times, uint8_t *peak) {
    nb_wake_event_t last = NB_WAKE_NONE;
    for (int i = 0; i < times; i++) {
        nb_wake_event_t e = nb_wake_detect_step(d, p, peak);
        if (e != NB_WAKE_NONE) {
            last = e;
        }
    }
    return last;
}

static void test_warmup_ignores_first_steps(void) {
    nb_wake_detect_t d = fresh();
    // Three warm-up steps, then four more: the window is still not full.
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 255, 7, NULL));
    NB_CHECK_EQ_INT(NB_WAKE_DETECTED, nb_wake_detect_step(&d, 255, NULL));
}

static void test_sustained_high_wakes_with_its_average(void) {
    nb_wake_detect_t d = fresh();
    feed(&d, 0, 3, NULL);
    feed(&d, 240, 4, NULL);
    uint8_t peak = 0;
    NB_CHECK_EQ_INT(NB_WAKE_DETECTED, nb_wake_detect_step(&d, 240, &peak));
    NB_CHECK_EQ_INT(240, peak);
}

static void test_single_blip_does_not_wake(void) {
    nb_wake_detect_t d = fresh();
    feed(&d, 0, 3 + 5, NULL);
    NB_CHECK_EQ_INT(NB_WAKE_NONE, nb_wake_detect_step(&d, 255, NULL));
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 0, 10, NULL));
}

static void test_average_must_pass_cutoff_strictly(void) {
    nb_wake_detect_t d = fresh();
    feed(&d, 0, 3, NULL);
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 230, 20, NULL));
}

static void test_detection_resets_into_warmup(void) {
    nb_wake_detect_t d = fresh();
    feed(&d, 0, 3, NULL);
    NB_CHECK_EQ_INT(NB_WAKE_DETECTED, feed(&d, 255, 5, NULL));
    NB_CHECK_EQ_INT(0, nb_wake_detect_average(&d));
    // Warm-up again, then a full window before the next detection.
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 255, 3 + 4, NULL));
    NB_CHECK_EQ_INT(NB_WAKE_DETECTED, nb_wake_detect_step(&d, 255, NULL));
}

static void test_near_miss_reports_peak_once_when_it_falls(void) {
    nb_wake_detect_t d = fresh();
    feed(&d, 0, 3 + 5, NULL);
    // Averages climb to 200 (window full of 200) and fall back.
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 200, 5, NULL));
    uint8_t peak = 0;
    nb_wake_event_t e = NB_WAKE_NONE;
    int events = 0;
    for (int i = 0; i < 10; i++) {
        nb_wake_event_t s = nb_wake_detect_step(&d, 0, &peak);
        if (s != NB_WAKE_NONE) {
            e = s;
            events++;
        }
    }
    NB_CHECK_EQ_INT(NB_WAKE_NEAR_MISS, e);
    NB_CHECK_EQ_INT(1, events);
    NB_CHECK_EQ_INT(200, peak);
}

static void test_quiet_input_reports_nothing(void) {
    nb_wake_detect_t d = fresh();
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 100, 50, NULL));
}

static void test_reset_drops_near_miss_in_progress(void) {
    nb_wake_detect_t d = fresh();
    feed(&d, 0, 3, NULL);
    feed(&d, 200, 5, NULL);
    nb_wake_detect_reset(&d);
    NB_CHECK_EQ_INT(NB_WAKE_NONE, feed(&d, 0, 20, NULL));
}

static void test_window_is_clamped(void) {
    nb_wake_detect_config_t cfg = kConfig;
    cfg.window = 0;
    cfg.warmup_steps = 0;
    nb_wake_detect_t d;
    nb_wake_detect_init(&d, &cfg);
    NB_CHECK_EQ_INT(1, d.cfg.window);
    NB_CHECK_EQ_INT(NB_WAKE_DETECTED, nb_wake_detect_step(&d, 255, NULL));
    cfg.window = 200;
    nb_wake_detect_init(&d, &cfg);
    NB_CHECK_EQ_INT(NB_WAKE_DETECT_MAX_WINDOW, d.cfg.window);
}

static void test_quantize_matches_training_range(void) {
    // Raw 0 is the bottom of the input range; raw ~666 is 26.0 in training
    // units, the top of the model's quantized range.
    NB_CHECK_EQ_INT(-128, nb_wake_quantize_feature(0, INPUT_SCALE, INPUT_ZP));
    NB_CHECK_EQ_INT(127, nb_wake_quantize_feature(666, INPUT_SCALE, INPUT_ZP));
    NB_CHECK_EQ_INT(127, nb_wake_quantize_feature(5000, INPUT_SCALE, INPUT_ZP));
    // Midpoint: raw 333 is 13.0, i.e. 127.5 steps above the zero point.
    int mid = nb_wake_quantize_feature(333, INPUT_SCALE, INPUT_ZP);
    NB_CHECK(mid == -1 || mid == 0);
}

static void test_quantize_agrees_with_integer_formula(void) {
    // microWakeWord's reference integer mapping: (raw * 256 + 333) / 666 - 128.
    int worst = 0;
    for (int raw = 0; raw <= 700; raw++) {
        int ref = (raw * 256 + 333) / 666 - 128;
        ref = ref > 127 ? 127 : ref;
        int got = nb_wake_quantize_feature((uint16_t)raw, INPUT_SCALE, INPUT_ZP);
        int diff = got > ref ? got - ref : ref - got;
        worst = diff > worst ? diff : worst;
    }
    NB_CHECK(worst <= 1);
}

static void test_quantize_bad_scale_gives_zero_point(void) {
    NB_CHECK_EQ_INT(-128, nb_wake_quantize_feature(400, 0.0f, INPUT_ZP));
}

static void test_text_mentions_wake_word(void) {
    NB_CHECK(nb_wake_text_mentions("Tchau, cuidado com o \"nexo\" ai.", "Nexo"));
    NB_CHECK(nb_wake_text_mentions("NEXO!", "Nexo"));
    NB_CHECK(nb_wake_text_mentions("Mandei o anexo.", "Nexo"));
    NB_CHECK(nb_wake_text_mentions("Nexo", "Nexo"));
    NB_CHECK(!nb_wake_text_mentions("Tudo certo por aqui.", "Nexo"));
    NB_CHECK(!nb_wake_text_mentions("Nex", "Nexo"));
    NB_CHECK(!nb_wake_text_mentions("", "Nexo"));
    NB_CHECK(!nb_wake_text_mentions(NULL, "Nexo"));
    NB_CHECK(!nb_wake_text_mentions("nexo", ""));
}

int main(void) {
    NB_RUN(test_warmup_ignores_first_steps);
    NB_RUN(test_sustained_high_wakes_with_its_average);
    NB_RUN(test_single_blip_does_not_wake);
    NB_RUN(test_average_must_pass_cutoff_strictly);
    NB_RUN(test_detection_resets_into_warmup);
    NB_RUN(test_near_miss_reports_peak_once_when_it_falls);
    NB_RUN(test_quiet_input_reports_nothing);
    NB_RUN(test_reset_drops_near_miss_in_progress);
    NB_RUN(test_window_is_clamped);
    NB_RUN(test_quantize_matches_training_range);
    NB_RUN(test_quantize_agrees_with_integer_formula);
    NB_RUN(test_quantize_bad_scale_gives_zero_point);
    NB_RUN(test_text_mentions_wake_word);
    NB_TEST_MAIN_END();
}
