#include "nb_test.h"
#include "nb_turn.h"

static nb_turn_out_t read_turn(nb_turn_t *t, bool audible, uint32_t now) {
    nb_turn_out_t out;
    nb_turn_update(t, audible, now, &out);
    return out;
}

static void test_hand_waits_for_the_delay_after_the_text(void) {
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    nb_turn_user_text(&t, 7u, 1000u);
    NB_CHECK(!read_turn(&t, false, 1000u + NB_TURN_HAND_DELAY_MS - 1u).wait_confirmed);
    NB_CHECK(read_turn(&t, false, 1000u + NB_TURN_HAND_DELAY_MS).wait_confirmed);
}

static void test_speaking_alone_ends_neither_the_wait_nor_starts_the_bubble(void) {
    // Speaking begins within 50 ms of the text, before any audio.
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    nb_turn_user_text(&t, 1u, 0u);
    nb_turn_device(&t, NB_TURN_DEVICE_SPEAKING);
    nb_turn_out_t out = read_turn(&t, false, 3000u);
    NB_CHECK(out.wait_confirmed);
    NB_CHECK(!out.speech_started);
}

static void test_first_sentence_ends_the_wait_and_starts_the_bubble(void) {
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    nb_turn_user_text(&t, 1u, 0u);
    nb_turn_device(&t, NB_TURN_DEVICE_SPEAKING);
    nb_turn_sentence(&t);
    nb_turn_out_t out = read_turn(&t, false, 3000u);
    NB_CHECK(!out.wait_confirmed);
    NB_CHECK(out.speech_started);
}

static void test_audible_playback_starts_the_bubble_without_a_sentence(void) {
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_device(&t, NB_TURN_DEVICE_SPEAKING);
    NB_CHECK(!read_turn(&t, false, 10u).speech_started);
    NB_CHECK(read_turn(&t, true, 20u).speech_started);
    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    NB_CHECK(!read_turn(&t, true, 30u).speech_started);  // the mic is open again
}

static void test_repeated_and_late_text_do_not_rearm(void) {
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_user_text(&t, 5u, 0u);
    nb_turn_user_text(&t, 5u, 2000u);  // the same text again: not a new wait
    NB_CHECK(read_turn(&t, false, NB_TURN_HAND_DELAY_MS).wait_confirmed);
    nb_turn_sentence(&t);
    nb_turn_user_text(&t, 6u, 4000u);  // late, during the answer
    nb_turn_out_t out = read_turn(&t, false, 9000u);
    NB_CHECK(!out.wait_confirmed);
    NB_CHECK(out.speech_started);
}

static void test_the_wait_gives_up_once_after_twenty_seconds(void) {
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_user_text(&t, 1u, 100u);
    NB_CHECK(read_turn(&t, false, 100u + NB_TURN_STALL_MS - 1u).wait_confirmed);
    nb_turn_out_t out = read_turn(&t, false, 100u + NB_TURN_STALL_MS);
    NB_CHECK(out.stalled_now && !out.wait_confirmed);
    out = read_turn(&t, false, 100u + NB_TURN_STALL_MS + 1000u);
    NB_CHECK(!out.stalled_now && !out.wait_confirmed);
    nb_turn_sentence(&t);  // a late answer still shows
    NB_CHECK(read_turn(&t, false, 100u + NB_TURN_STALL_MS + 2000u).speech_started);
}

static void test_idle_error_and_a_spoken_turn_ending_clear_the_turn(void) {
    nb_turn_t t;
    nb_turn_init(&t);
    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    nb_turn_user_text(&t, 1u, 0u);
    nb_turn_device(&t, NB_TURN_DEVICE_IDLE);
    NB_CHECK(!read_turn(&t, false, 5000u).wait_confirmed);

    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    nb_turn_user_text(&t, 2u, 6000u);
    nb_turn_device(&t, NB_TURN_DEVICE_OTHER);  // an error or a reconnection
    NB_CHECK(!read_turn(&t, false, 12000u).wait_confirmed);

    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);
    nb_turn_user_text(&t, 3u, 13000u);
    nb_turn_device(&t, NB_TURN_DEVICE_SPEAKING);
    nb_turn_sentence(&t);
    nb_turn_device(&t, NB_TURN_DEVICE_LISTENING);  // it finished speaking
    NB_CHECK(!read_turn(&t, false, 20000u).speech_started);
    nb_turn_user_text(&t, 3u, 21000u);  // the next turn, even with the same words
    NB_CHECK(read_turn(&t, false, 21000u + NB_TURN_HAND_DELAY_MS).wait_confirmed);
}

static void test_null_calls_are_safe(void) {
    nb_turn_out_t out;
    nb_turn_init(NULL);
    nb_turn_user_text(NULL, 1u, 0u);
    nb_turn_sentence(NULL);
    nb_turn_device(NULL, NB_TURN_DEVICE_IDLE);
    nb_turn_update(NULL, true, 0u, &out);
    NB_CHECK(!out.wait_confirmed && !out.speech_started && !out.stalled_now);
    nb_turn_update(NULL, true, 0u, NULL);
    nb_turn_reset(NULL);
}

int main(void) {
    NB_RUN(test_hand_waits_for_the_delay_after_the_text);
    NB_RUN(test_speaking_alone_ends_neither_the_wait_nor_starts_the_bubble);
    NB_RUN(test_first_sentence_ends_the_wait_and_starts_the_bubble);
    NB_RUN(test_audible_playback_starts_the_bubble_without_a_sentence);
    NB_RUN(test_repeated_and_late_text_do_not_rearm);
    NB_RUN(test_the_wait_gives_up_once_after_twenty_seconds);
    NB_RUN(test_idle_error_and_a_spoken_turn_ending_clear_the_turn);
    NB_RUN(test_null_calls_are_safe);
    NB_TEST_MAIN_END();
}
