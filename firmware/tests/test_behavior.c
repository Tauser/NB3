#include "nb_arbiter.h"
#include "nb_behavior.h"
#include "nb_test.h"

static void test_starts_idle(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_IDLE, b.state);
    NB_CHECK(!b.quiet);
}

static void test_conversation_engages_and_returns_to_idle(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    nb_behavior_transition_t t = nb_behavior_device(&b, NB_DEVICE_BUSY, 0);
    NB_CHECK(t.changed);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_ENGAGED, t.to);
    // Listening, speaking, listening again: still the same conversation.
    NB_CHECK(!nb_behavior_device(&b, NB_DEVICE_BUSY, 10).changed);
    t = nb_behavior_device(&b, NB_DEVICE_IDLE, 20);
    NB_CHECK(t.changed);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_ENGAGED, t.from);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_IDLE, t.to);
}

static void test_touch_reacts_and_lingers(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_REACTING, nb_behavior_touch(&b, true, 0).to);
    NB_CHECK(!nb_behavior_touch(&b, false, 1000).changed);  // still reacting
    NB_CHECK(!nb_behavior_tick(&b, 2999).changed);
    nb_behavior_transition_t t = nb_behavior_tick(&b, 3000);
    NB_CHECK(t.changed);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_IDLE, t.to);
}

static void test_touch_again_while_lingering_keeps_reacting(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    nb_behavior_touch(&b, true, 0);
    nb_behavior_touch(&b, false, 100);
    nb_behavior_touch(&b, true, 1500);
    NB_CHECK(!nb_behavior_tick(&b, 4000).changed);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_REACTING, b.state);
}

static void test_conversation_wins_over_touch(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    nb_behavior_device(&b, NB_DEVICE_BUSY, 0);
    NB_CHECK(!nb_behavior_touch(&b, true, 10).changed);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_ENGAGED, b.state);
    // Conversation ends while the hand is still there: back to the touch.
    NB_CHECK_EQ_INT(NB_BEHAVIOR_REACTING, nb_behavior_device(&b, NB_DEVICE_IDLE, 20).to);
}

static void test_sleep_only_from_idle_and_anything_wakes(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    nb_behavior_device(&b, NB_DEVICE_BUSY, 0);
    NB_CHECK(!nb_behavior_sleep(&b).changed);  // not in a conversation
    nb_behavior_device(&b, NB_DEVICE_IDLE, 10);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_SLEEPING, nb_behavior_sleep(&b).to);
    NB_CHECK(!nb_behavior_device(&b, NB_DEVICE_IDLE, 20).changed);  // stays asleep
    NB_CHECK_EQ_INT(NB_BEHAVIOR_ENGAGED, nb_behavior_device(&b, NB_DEVICE_BUSY, 30).to);

    nb_behavior_device(&b, NB_DEVICE_IDLE, 40);
    nb_behavior_sleep(&b);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_REACTING, nb_behavior_touch(&b, true, 50).to);

    nb_behavior_touch(&b, false, 60);
    nb_behavior_tick(&b, 5000);
    nb_behavior_sleep(&b);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_IDLE, nb_behavior_wake(&b).to);
    NB_CHECK(!nb_behavior_wake(&b).changed);
}

static void test_fault_is_sticky_until_recovered(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_FAULT, nb_behavior_device(&b, NB_DEVICE_FAULT, 0).to);
    NB_CHECK(!nb_behavior_touch(&b, true, 10).changed);
    NB_CHECK(!nb_behavior_sleep(&b).changed);
    NB_CHECK(!nb_behavior_recover(&b, 20).changed);  // the device still reports it
    nb_behavior_touch(&b, false, 30);
    NB_CHECK(!nb_behavior_device(&b, NB_DEVICE_IDLE, 40).changed);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_FAULT, b.state);
    nb_behavior_tick(&b, 9000);
    NB_CHECK_EQ_INT(NB_BEHAVIOR_IDLE, nb_behavior_recover(&b, 9100).to);
}

static void test_quiet_is_a_mode_that_survives_transitions(void) {
    nb_behavior_t b;
    nb_behavior_init(&b);
    nb_behavior_set_quiet(&b, true);
    nb_behavior_device(&b, NB_DEVICE_BUSY, 0);
    nb_behavior_device(&b, NB_DEVICE_FAULT, 10);
    nb_behavior_device(&b, NB_DEVICE_IDLE, 20);
    nb_behavior_recover(&b, 30);
    NB_CHECK(b.quiet);
    nb_behavior_set_quiet(&b, false);
    NB_CHECK(!b.quiet);
}

// The IDLE invariant end to end: every way back to IDLE clears what is
// transient in the arbiter and keeps the mood, the way the shell applies it.
typedef struct {
    nb_behavior_t behavior;
    nb_arbiter_t arbiter;
} rig_t;

static void rig_init(rig_t *r) {
    nb_behavior_init(&r->behavior);
    nb_arbiter_init(&r->arbiter, NB_EXPR_NEUTRAL);
    nb_proposal_t mood = {NB_EXPR_CONTENT, 3, 0, false};
    nb_arbiter_propose(&r->arbiter, NB_SOURCE_MOOD, &mood, 0);
}

static void rig_apply(rig_t *r, nb_behavior_transition_t t) {
    if (t.changed && t.to == NB_BEHAVIOR_IDLE) {
        nb_arbiter_enter_idle(&r->arbiter);
    }
}

static void rig_dirty(rig_t *r) {
    nb_proposal_t touch = {NB_EXPR_LOVING, 40, 0, true};
    nb_proposal_t emotion = {NB_EXPR_HAPPY, 20, 0, true};
    nb_arbiter_propose(&r->arbiter, NB_SOURCE_TOUCH, &touch, 0);
    nb_arbiter_propose(&r->arbiter, NB_SOURCE_EMOTION, &emotion, 0);
}

static void check_clean_with_mood(rig_t *r, const char *path) {
    if (r->behavior.state != NB_BEHAVIOR_IDLE ||
        nb_arbiter_resolve(&r->arbiter, 0) != NB_EXPR_CONTENT) {
        fprintf(stderr, "IDLE invariant broken coming from %s\n", path);
    }
    NB_CHECK_EQ_INT(NB_BEHAVIOR_IDLE, r->behavior.state);
    NB_CHECK_EQ_INT(NB_EXPR_CONTENT, nb_arbiter_resolve(&r->arbiter, 0));
}

static void test_every_return_to_idle_clears_transient_and_keeps_mood(void) {
    rig_t r;

    rig_init(&r);  // from ENGAGED
    rig_apply(&r, nb_behavior_device(&r.behavior, NB_DEVICE_BUSY, 0));
    rig_dirty(&r);
    rig_apply(&r, nb_behavior_device(&r.behavior, NB_DEVICE_IDLE, 10));
    check_clean_with_mood(&r, "engaged");

    rig_init(&r);  // from REACTING, when the reaction ends
    rig_apply(&r, nb_behavior_touch(&r.behavior, true, 0));
    rig_dirty(&r);
    rig_apply(&r, nb_behavior_touch(&r.behavior, false, 10));
    rig_apply(&r, nb_behavior_tick(&r.behavior, 10 + NB_BEHAVIOR_REACTION_LINGER_MS));
    check_clean_with_mood(&r, "reacting");

    rig_init(&r);  // from SLEEPING, on wake
    rig_apply(&r, nb_behavior_sleep(&r.behavior));
    rig_dirty(&r);
    rig_apply(&r, nb_behavior_wake(&r.behavior));
    check_clean_with_mood(&r, "sleeping");

    rig_init(&r);  // from FAULT, on recovery
    rig_apply(&r, nb_behavior_device(&r.behavior, NB_DEVICE_FAULT, 0));
    rig_dirty(&r);
    rig_apply(&r, nb_behavior_device(&r.behavior, NB_DEVICE_IDLE, 10));
    rig_apply(&r, nb_behavior_recover(&r.behavior, 20));
    check_clean_with_mood(&r, "fault");
}

static void test_names_and_null(void) {
    NB_CHECK_EQ_STR("engaged", nb_behavior_state_name(NB_BEHAVIOR_ENGAGED));
    NB_CHECK_EQ_STR("unknown", nb_behavior_state_name(NB_BEHAVIOR_COUNT));
    nb_behavior_init(NULL);
    NB_CHECK(!nb_behavior_device(NULL, NB_DEVICE_BUSY, 0).changed);
    NB_CHECK(!nb_behavior_touch(NULL, true, 0).changed);
    NB_CHECK(!nb_behavior_sleep(NULL).changed);
    NB_CHECK(!nb_behavior_wake(NULL).changed);
    NB_CHECK(!nb_behavior_recover(NULL, 0).changed);
    NB_CHECK(!nb_behavior_tick(NULL, 0).changed);
    nb_behavior_set_quiet(NULL, true);
}

int main(void) {
    NB_RUN(test_starts_idle);
    NB_RUN(test_conversation_engages_and_returns_to_idle);
    NB_RUN(test_touch_reacts_and_lingers);
    NB_RUN(test_touch_again_while_lingering_keeps_reacting);
    NB_RUN(test_conversation_wins_over_touch);
    NB_RUN(test_sleep_only_from_idle_and_anything_wakes);
    NB_RUN(test_fault_is_sticky_until_recovered);
    NB_RUN(test_quiet_is_a_mode_that_survives_transitions);
    NB_RUN(test_every_return_to_idle_clears_transient_and_keeps_mood);
    NB_RUN(test_names_and_null);
    NB_TEST_MAIN_END();
}
