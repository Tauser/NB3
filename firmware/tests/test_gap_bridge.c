#include "nb_gap_bridge.h"
#include "nb_test.h"

#define FRAME_MS 20u

static nb_gap_bridge_t make(uint32_t hold_ms, uint32_t preroll_ms) {
    const nb_gap_bridge_config_t cfg = {hold_ms, preroll_ms};
    nb_gap_bridge_t b;
    nb_gap_bridge_init(&b, &cfg);
    return b;
}

static void silence(nb_gap_bridge_t *b, uint32_t ms, nb_gap_action_t expected) {
    for (uint32_t t = 0; t < ms; t += FRAME_MS) {
        NB_CHECK_EQ_INT(expected, nb_gap_bridge_step(b, false, FRAME_MS));
    }
}

static void test_silence_before_the_first_word_is_sent(void) {
    nb_gap_bridge_t b = make(700, 300);
    silence(&b, 2000, NB_GAP_SEND);
}

static void test_speech_is_always_sent(void) {
    nb_gap_bridge_t b = make(700, 300);
    for (int i = 0; i < 50; i++) {
        NB_CHECK_EQ_INT(NB_GAP_SEND, nb_gap_bridge_step(&b, true, FRAME_MS));
    }
}

static void test_a_short_pause_is_held_and_the_return_flushes_the_preroll(void) {
    nb_gap_bridge_t b = make(700, 300);
    nb_gap_bridge_step(&b, true, FRAME_MS);
    silence(&b, 500, NB_GAP_HOLD);
    NB_CHECK_EQ_INT(NB_GAP_FLUSH_AND_SEND, nb_gap_bridge_step(&b, true, FRAME_MS));
    // Afterwards it is plain sending again.
    NB_CHECK_EQ_INT(NB_GAP_SEND, nb_gap_bridge_step(&b, true, FRAME_MS));
}

static void test_a_pause_exactly_at_the_limit_is_still_held(void) {
    nb_gap_bridge_t b = make(700, 300);
    nb_gap_bridge_step(&b, true, FRAME_MS);
    silence(&b, 700, NB_GAP_HOLD);
    NB_CHECK_EQ_INT(NB_GAP_FLUSH_AND_SEND, nb_gap_bridge_step(&b, true, FRAME_MS));
}

static void test_a_long_silence_ends_the_hold_so_the_server_sees_the_end(void) {
    nb_gap_bridge_t b = make(700, 300);
    nb_gap_bridge_step(&b, true, FRAME_MS);
    silence(&b, 700, NB_GAP_HOLD);
    silence(&b, 1000, NB_GAP_SEND);  // live again, silence reaches the server
    // Speech after giving up needs no flush: nothing is held.
    NB_CHECK_EQ_INT(NB_GAP_SEND, nb_gap_bridge_step(&b, true, FRAME_MS));
}

static void test_each_pause_gets_its_own_allowance(void) {
    nb_gap_bridge_t b = make(700, 300);
    nb_gap_bridge_step(&b, true, FRAME_MS);
    for (int round = 0; round < 3; round++) {
        silence(&b, 600, NB_GAP_HOLD);
        NB_CHECK_EQ_INT(NB_GAP_FLUSH_AND_SEND, nb_gap_bridge_step(&b, true, FRAME_MS));
    }
}

static void test_reset_starts_a_new_turn(void) {
    nb_gap_bridge_t b = make(700, 300);
    nb_gap_bridge_step(&b, true, FRAME_MS);
    silence(&b, 200, NB_GAP_HOLD);
    nb_gap_bridge_reset(&b);
    silence(&b, 1000, NB_GAP_SEND);  // nothing heard yet in the new turn
}

static void test_disabled_bridge_sends_everything(void) {
    nb_gap_bridge_t b = make(0, 300);
    nb_gap_bridge_step(&b, true, FRAME_MS);
    silence(&b, 1000, NB_GAP_SEND);
}

static void test_preroll_frames_round_up(void) {
    nb_gap_bridge_t b = make(700, 300);
    NB_CHECK_EQ_INT(15, (int)nb_gap_bridge_preroll_frames(&b, 20));
    NB_CHECK_EQ_INT(10, (int)nb_gap_bridge_preroll_frames(&b, 30));
    NB_CHECK_EQ_INT(0, (int)nb_gap_bridge_preroll_frames(&b, 0));
    b = make(700, 310);
    NB_CHECK_EQ_INT(16, (int)nb_gap_bridge_preroll_frames(&b, 20));
}

static void test_null_is_safe(void) {
    nb_gap_bridge_init(NULL, NULL);
    nb_gap_bridge_reset(NULL);
    NB_CHECK_EQ_INT(NB_GAP_SEND, nb_gap_bridge_step(NULL, false, FRAME_MS));
    NB_CHECK_EQ_INT(0, (int)nb_gap_bridge_preroll_frames(NULL, FRAME_MS));
}

int main(void) {
    NB_RUN(test_silence_before_the_first_word_is_sent);
    NB_RUN(test_speech_is_always_sent);
    NB_RUN(test_a_short_pause_is_held_and_the_return_flushes_the_preroll);
    NB_RUN(test_a_pause_exactly_at_the_limit_is_still_held);
    NB_RUN(test_a_long_silence_ends_the_hold_so_the_server_sees_the_end);
    NB_RUN(test_each_pause_gets_its_own_allowance);
    NB_RUN(test_reset_starts_a_new_turn);
    NB_RUN(test_disabled_bridge_sends_everything);
    NB_RUN(test_preroll_frames_round_up);
    NB_RUN(test_null_is_safe);
    NB_TEST_MAIN_END();
}
