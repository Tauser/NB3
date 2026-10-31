#include "nb_output_activity.h"
#include "nb_test.h"

static void test_pending_dma_is_protected_before_first_callback(void) {
    nb_output_activity_t s = {0};
    NB_CHECK(!nb_output_activity_active(&s, 10, 60, 120));
    nb_output_activity_written(&s, 100);
    NB_CHECK(nb_output_activity_active(&s, 100, 60, 120));
    NB_CHECK(nb_output_activity_active(&s, 279, 60, 120));
    NB_CHECK(!nb_output_activity_active(&s, 280, 60, 120));
}

static void test_dma_signal_extends_tail_even_after_server_stop(void) {
    nb_output_activity_t s = {0};
    nb_output_activity_written(&s, 100);
    nb_output_activity_played(&s, 200);
    // A logical TTS STOP is deliberately not an input to the physical tail.
    NB_CHECK(nb_output_activity_active(&s, 319, 60, 120));
    NB_CHECK(!nb_output_activity_active(&s, 320, 60, 120));
}

static void test_new_write_and_clock_wrap(void) {
    nb_output_activity_t s = {0};
    nb_output_activity_written(&s, UINT32_MAX - 20);
    NB_CHECK(nb_output_activity_active(&s, 10, 60, 120));
    NB_CHECK(!nb_output_activity_active(&s, 200, 60, 120));
    nb_output_activity_played(&s, 201);
    NB_CHECK(nb_output_activity_active(&s, 250, 60, 120));
    NB_CHECK(!nb_output_activity_active(&s, 321, 60, 120));
}

int main(void) {
    NB_RUN(test_pending_dma_is_protected_before_first_callback);
    NB_RUN(test_dma_signal_extends_tail_even_after_server_stop);
    NB_RUN(test_new_write_and_clock_wrap);
    NB_TEST_MAIN_END();
}
