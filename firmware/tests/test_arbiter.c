#include "nb_arbiter.h"
#include "nb_test.h"

static nb_proposal_t proposal(nb_expression_t e, uint8_t priority, uint32_t ttl_ms, bool transient) {
    nb_proposal_t p = {e, priority, ttl_ms, transient};
    return p;
}

static void test_baseline_when_nothing_proposed(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_arbiter_resolve(&a, 0));
}

static void test_higher_priority_wins(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t emotion = proposal(NB_EXPR_HAPPY, 10, 0, true);
    nb_proposal_t reflex = proposal(NB_EXPR_SURPRISED, 50, 0, true);
    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_REFLEX, &reflex, 0));
    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 1));
    NB_CHECK_EQ_INT(NB_EXPR_SURPRISED, nb_arbiter_resolve(&a, 2));
}

static void test_equal_priority_goes_to_most_recent(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t state = proposal(NB_EXPR_THINKING, 10, 0, true);
    nb_proposal_t emotion = proposal(NB_EXPR_HAPPY, 10, 0, true);
    nb_arbiter_propose(&a, NB_SOURCE_STATE, &state, 0);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 5);
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_arbiter_resolve(&a, 6));
    nb_arbiter_propose(&a, NB_SOURCE_STATE, &state, 7);
    NB_CHECK_EQ_INT(NB_EXPR_THINKING, nb_arbiter_resolve(&a, 8));
}

static void test_same_source_replaces_its_proposal(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t sad = proposal(NB_EXPR_SAD, 10, 0, true);
    nb_proposal_t happy = proposal(NB_EXPR_HAPPY, 10, 0, true);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &sad, 0);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &happy, 1);
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_arbiter_resolve(&a, 2));
}

static void test_expired_proposal_falls_back(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t emotion = proposal(NB_EXPR_HAPPY, 10, 0, true);
    nb_proposal_t touch = proposal(NB_EXPR_LOVING, 30, 500, true);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 0);
    nb_arbiter_propose(&a, NB_SOURCE_TOUCH, &touch, 100);
    NB_CHECK_EQ_INT(NB_EXPR_LOVING, nb_arbiter_resolve(&a, 599));
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_arbiter_resolve(&a, 600));
}

static void test_expiry_survives_clock_wraparound(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t touch = proposal(NB_EXPR_LOVING, 30, 1000, true);
    nb_arbiter_propose(&a, NB_SOURCE_TOUCH, &touch, UINT32_MAX - 100);
    NB_CHECK_EQ_INT(NB_EXPR_LOVING, nb_arbiter_resolve(&a, 50));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_arbiter_resolve(&a, 900));
}

static void test_idle_clears_transient_and_keeps_persistent(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t mood = proposal(NB_EXPR_SLEEPY, 5, 0, false);
    nb_proposal_t emotion = proposal(NB_EXPR_ANGRY, 10, 0, true);
    nb_arbiter_propose(&a, NB_SOURCE_STATE, &mood, 0);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 1);
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, nb_arbiter_resolve(&a, 2));
    nb_arbiter_enter_idle(&a);
    NB_CHECK_EQ_INT(NB_EXPR_SLEEPY, nb_arbiter_resolve(&a, 3));
}

static void test_idle_with_only_transient_returns_baseline(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t emotion = proposal(NB_EXPR_SAD, 10, 0, true);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 0);
    nb_arbiter_enter_idle(&a);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_arbiter_resolve(&a, 1));
}

// The MCP face command: timed, not transient, below touch.
static void test_timed_command_survives_idle_and_resumes_after_touch(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t command = proposal(NB_EXPR_ANGRY, 30, 30000, false);
    nb_proposal_t touch = proposal(NB_EXPR_HAPPY, 40, 1500, true);
    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_COMMAND, &command, 0));

    nb_arbiter_enter_idle(&a);
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, nb_arbiter_resolve(&a, 5000));

    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_TOUCH, &touch, 10000));
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_arbiter_resolve(&a, 10500));
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, nb_arbiter_resolve(&a, 11600));

    // The deadline kept running under the touch: it ends at 30 s, not later.
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, nb_arbiter_resolve(&a, 29999));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_arbiter_resolve(&a, 30000));
}

// The mood: lowest priority, not transient, no expiry.
static void test_mood_shows_at_rest_and_survives_idle(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t mood = proposal(NB_EXPR_ANGRY, 3, 0, false);
    nb_proposal_t listening = proposal(NB_EXPR_LISTENING, 5, 0, true);
    nb_proposal_t emotion = proposal(NB_EXPR_HAPPY, 20, 0, true);
    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_MOOD, &mood, 0));
    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_STATE, &listening, 1));
    NB_CHECK_EQ_INT(NB_EXPR_LISTENING, nb_arbiter_resolve(&a, 2));
    NB_CHECK(nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 3));
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_arbiter_resolve(&a, 4));

    nb_arbiter_enter_idle(&a);
    nb_arbiter_withdraw(&a, NB_SOURCE_STATE);
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, nb_arbiter_resolve(&a, 600000));
}

static void test_withdraw_removes_only_that_source(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t emotion = proposal(NB_EXPR_HAPPY, 10, 0, true);
    nb_proposal_t touch = proposal(NB_EXPR_LOVING, 30, 0, true);
    nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &emotion, 0);
    nb_arbiter_propose(&a, NB_SOURCE_TOUCH, &touch, 1);
    nb_arbiter_withdraw(&a, NB_SOURCE_TOUCH);
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_arbiter_resolve(&a, 2));
}

static void test_invalid_input_is_rejected(void) {
    nb_arbiter_t a;
    nb_arbiter_init(&a, NB_EXPR_NEUTRAL);
    nb_proposal_t bad_expr = proposal(NB_EXPR_COUNT, 10, 0, true);
    nb_proposal_t ok = proposal(NB_EXPR_HAPPY, 10, 0, true);
    NB_CHECK(!nb_arbiter_propose(&a, NB_SOURCE_EMOTION, &bad_expr, 0));
    NB_CHECK(!nb_arbiter_propose(&a, NB_SOURCE_COUNT, &ok, 0));
    NB_CHECK(!nb_arbiter_propose(&a, NB_SOURCE_EMOTION, NULL, 0));
    nb_arbiter_withdraw(&a, NB_SOURCE_COUNT);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_arbiter_resolve(&a, 1));
}

int main(void) {
    NB_RUN(test_baseline_when_nothing_proposed);
    NB_RUN(test_higher_priority_wins);
    NB_RUN(test_equal_priority_goes_to_most_recent);
    NB_RUN(test_same_source_replaces_its_proposal);
    NB_RUN(test_expired_proposal_falls_back);
    NB_RUN(test_expiry_survives_clock_wraparound);
    NB_RUN(test_idle_clears_transient_and_keeps_persistent);
    NB_RUN(test_idle_with_only_transient_returns_baseline);
    NB_RUN(test_timed_command_survives_idle_and_resumes_after_touch);
    NB_RUN(test_mood_shows_at_rest_and_survives_idle);
    NB_RUN(test_withdraw_removes_only_that_source);
    NB_RUN(test_invalid_input_is_rejected);
    NB_TEST_MAIN_END();
}
