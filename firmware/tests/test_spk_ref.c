#include "nb_spk_ref.h"
#include "nb_test.h"

#define CAP 64
#define BLOCK 8

static int16_t history[CAP];

// Plays BLOCK samples counting up from `first`, so a reference sample tells
// which played sample it came from.
static void play_block(nb_spk_ref_t *r, int first) {
    int16_t s[BLOCK];
    for (int i = 0; i < BLOCK; i++) {
        s[i] = (int16_t)(first + i);
    }
    nb_spk_ref_played(r, s, BLOCK);
}

static nb_spk_ref_t fresh(uint32_t lead) {
    nb_spk_ref_t r;
    nb_spk_ref_init(&r, history, CAP, lead);
    nb_spk_ref_set_output(&r, true);
    return r;
}

static void test_silence_before_any_measurement(void) {
    nb_spk_ref_t r = fresh(0);
    int16_t out[4] = {9, 9, 9, 9};
    nb_spk_ref_read(&r, out, 4);
    NB_CHECK(out[0] == 0 && out[3] == 0);
}

static void test_reference_lines_up_with_capture_time(void) {
    nb_spk_ref_t r = fresh(0);
    // The speaker started 16 samples before the mic: when the mic has
    // captured its first block, 24 samples have been played.
    play_block(&r, 1);
    play_block(&r, 9);
    play_block(&r, 17);
    nb_spk_ref_captured(&r, BLOCK);
    // Mic sample 0 was captured while played sample 16 (value 17) went out.
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(17, out[0]);
    NB_CHECK_EQ_INT(24, out[7]);
}

static void test_lead_moves_reference_earlier(void) {
    nb_spk_ref_t r = fresh(4);
    play_block(&r, 1);
    play_block(&r, 9);
    play_block(&r, 17);
    nb_spk_ref_captured(&r, BLOCK);
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(13, out[0]);
}

static void test_not_yet_played_is_silence(void) {
    nb_spk_ref_t r = fresh(0);
    play_block(&r, 1);
    nb_spk_ref_captured(&r, BLOCK);  // offset 0: played and captured together
    // The reader asks for 16 samples but only 8 were played so far.
    int16_t out[16];
    nb_spk_ref_read(&r, out, 16);
    NB_CHECK_EQ_INT(1, out[0]);
    NB_CHECK_EQ_INT(8, out[7]);
    NB_CHECK_EQ_INT(0, out[8]);
}

static void test_older_than_history_is_silence(void) {
    nb_spk_ref_t r = fresh(0);
    play_block(&r, 1);
    nb_spk_ref_captured(&r, BLOCK);
    // The reader falls far behind: the speaker plays a whole history more.
    for (int b = 1; b <= CAP / BLOCK; b++) {
        play_block(&r, 1 + b * BLOCK);
    }
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(0, out[0]);
}

static void test_dropped_capture_keeps_alignment(void) {
    nb_spk_ref_t r = fresh(0);
    play_block(&r, 1);
    nb_spk_ref_captured(&r, BLOCK);
    play_block(&r, 9);
    nb_spk_ref_captured(&r, BLOCK);
    // The first captured block was dropped: the next read is mic sample 8.
    nb_spk_ref_dropped(&r, BLOCK);
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(9, out[0]);
}

static void test_input_restart_skips_to_next_capture(void) {
    nb_spk_ref_t r = fresh(0);
    play_block(&r, 1);
    nb_spk_ref_captured(&r, BLOCK);
    nb_spk_ref_input_restarted(&r);
    play_block(&r, 9);
    nb_spk_ref_captured(&r, BLOCK);
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(9, out[0]);
}

static void test_output_off_gives_silence_until_measured_again(void) {
    nb_spk_ref_t r = fresh(0);
    play_block(&r, 1);
    nb_spk_ref_captured(&r, BLOCK);
    nb_spk_ref_set_output(&r, false);
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(0, out[0]);
    // Back on, a capture alone does not resync: the speaker must play first.
    nb_spk_ref_set_output(&r, true);
    nb_spk_ref_captured(&r, BLOCK);
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(0, out[0]);
    play_block(&r, 9);
    nb_spk_ref_captured(&r, BLOCK);
    // played 16, captured 24: offset -8. Next mic sample is 16 -> played 8.
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(9, out[0]);
}

static void test_counters_wrap(void) {
    nb_spk_ref_t r = fresh(0);
    r.played = r.captured = r.consumed = 0xFFFFFFF8u;
    play_block(&r, 100);
    play_block(&r, 108);
    nb_spk_ref_captured(&r, BLOCK);
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(108, out[0]);
}

static void test_bad_capacity_disables(void) {
    nb_spk_ref_t r;
    nb_spk_ref_init(&r, history, 48, 0);
    nb_spk_ref_set_output(&r, true);
    play_block(&r, 1);
    nb_spk_ref_captured(&r, BLOCK);
    int16_t out[BLOCK];
    nb_spk_ref_read(&r, out, BLOCK);
    NB_CHECK_EQ_INT(0, out[0]);
}

int main(void) {
    NB_RUN(test_silence_before_any_measurement);
    NB_RUN(test_reference_lines_up_with_capture_time);
    NB_RUN(test_lead_moves_reference_earlier);
    NB_RUN(test_not_yet_played_is_silence);
    NB_RUN(test_older_than_history_is_silence);
    NB_RUN(test_dropped_capture_keeps_alignment);
    NB_RUN(test_input_restart_skips_to_next_capture);
    NB_RUN(test_output_off_gives_silence_until_measured_again);
    NB_RUN(test_counters_wrap);
    NB_RUN(test_bad_capacity_disables);
    NB_TEST_MAIN_END();
}
