#include "nb_arbiter.h"
#include "nb_avatar.h"
#include "nb_face_link.h"
#include "nb_test.h"

// Arbiter and avatar together, driven the way the face display drives them:
// every frame the winner is resolved, the avatar is told only when it
// changes, then the avatar advances.
typedef struct {
    nb_arbiter_t arbiter;
    nb_avatar_core_state_t avatar;
    nb_expression_t shown;
    uint32_t now_ms;
} rig_t;

static void rig_init(rig_t *rig)
{
    nb_arbiter_init(&rig->arbiter, NB_EXPR_NEUTRAL);
    nb_avatar_core_init(&rig->avatar, 21u);
    rig->shown = NB_EXPR_NEUTRAL;
    rig->now_ms = 0u;
}

static void rig_run(rig_t *rig, uint32_t ms)
{
    const uint32_t frame_ms = 33u;
    for (uint32_t elapsed = 0u; elapsed < ms; elapsed += frame_ms) {
        const nb_expression_t winner = nb_arbiter_resolve(&rig->arbiter, rig->now_ms);
        if (winner != rig->shown) {
            rig->shown = winner;
            NB_CHECK(nb_face_link_show(&rig->avatar, winner));
        }
        nb_avatar_core_tick(&rig->avatar, frame_ms);
        rig->now_ms += frame_ms;
    }
}

static void rig_propose(rig_t *rig, nb_source_t source, nb_expression_t expression,
                        uint8_t priority, uint32_t ttl_ms)
{
    nb_proposal_t proposal = {expression, priority, ttl_ms, true};
    NB_CHECK(nb_arbiter_propose(&rig->arbiter, source, &proposal, rig->now_ms));
}

static void rig_propose_timed(rig_t *rig, nb_source_t source, nb_expression_t expression,
                              uint8_t priority, uint32_t ttl_ms)
{
    nb_proposal_t proposal = {expression, priority, ttl_ms, false};
    NB_CHECK(nb_arbiter_propose(&rig->arbiter, source, &proposal, rig->now_ms));
}

static nb_avatar_expression_t on_screen(const rig_t *rig)
{
    nb_avatar_render_frame_t frame;
    nb_avatar_core_get_frame(&rig->avatar, &frame);
    return frame.expression;
}

static void test_command_holds_face_for_its_whole_duration(void)
{
    rig_t rig;
    rig_init(&rig);
    rig_propose(&rig, NB_SOURCE_COMMAND, NB_EXPR_HAPPY, 30, 60000u);
    rig_run(&rig, 5000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_HAPPY);
    rig_run(&rig, 50000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_HAPPY);
    rig_run(&rig, 6000u);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, rig.shown);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_IDLE);
}

static void test_command_outlives_idle_and_returns_after_touch(void)
{
    rig_t rig;
    rig_init(&rig);
    rig_propose_timed(&rig, NB_SOURCE_COMMAND, NB_EXPR_ANGRY, 30, 30000u);
    rig_run(&rig, 3000u);
    nb_arbiter_enter_idle(&rig.arbiter);
    rig_run(&rig, 3000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_ANGRY);

    rig_propose(&rig, NB_SOURCE_TOUCH, NB_EXPR_HAPPY, 40, 1500u);
    rig_run(&rig, 500u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_HAPPY);
    rig_run(&rig, 1500u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_ANGRY);

    rig_run(&rig, 23500u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_IDLE);
}

static void test_short_reaction_finishes_its_gesture(void)
{
    rig_t rig;
    rig_init(&rig);
    rig_propose(&rig, NB_SOURCE_TOUCH, NB_EXPR_HAPPY, 40, 600u);
    rig_run(&rig, 700u);
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, rig.shown);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_HAPPY);
    rig_run(&rig, 3000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_IDLE);
}

static void test_loving_holds_while_petting_and_idle_clears_it(void)
{
    rig_t rig;
    rig_init(&rig);
    rig_propose(&rig, NB_SOURCE_TOUCH, NB_EXPR_LOVING, 40, 0u);
    rig_run(&rig, 20000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_LOVING);
    nb_arbiter_enter_idle(&rig.arbiter);
    rig_run(&rig, 3000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_IDLE);
}

static void test_new_winner_replaces_held_expression_at_once(void)
{
    rig_t rig;
    rig_init(&rig);
    rig_propose(&rig, NB_SOURCE_EMOTION, NB_EXPR_HAPPY, 20, 0u);
    rig_run(&rig, 500u);
    rig_propose(&rig, NB_SOURCE_COMMAND, NB_EXPR_SAD, 30, 10000u);
    rig_run(&rig, 100u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_SAD);
    // When the command expires the emotion underneath is still winning.
    rig_run(&rig, 10000u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_HAPPY);
}

static void test_state_expressions_switch_without_waiting(void)
{
    rig_t rig;
    rig_init(&rig);
    rig_propose(&rig, NB_SOURCE_EMOTION, NB_EXPR_SURPRISED, 20, 0u);
    rig_run(&rig, 200u);
    nb_arbiter_withdraw(&rig.arbiter, NB_SOURCE_EMOTION);
    rig_propose(&rig, NB_SOURCE_STATE, NB_EXPR_LISTENING, 5, 0u);
    rig_run(&rig, 100u);
    NB_CHECK(on_screen(&rig) == NB_AVATAR_EXPR_LISTENING);
}

static void test_every_expression_maps_to_an_animation(void)
{
    for (int e = 0; e < NB_EXPR_COUNT; ++e) {
        NB_CHECK(nb_face_link_avatar_for((nb_expression_t)e) < NB_AVATAR_EXPR_COUNT);
    }
    NB_CHECK(nb_face_link_avatar_for(NB_EXPR_NEUTRAL) == NB_AVATAR_EXPR_IDLE);
    NB_CHECK(nb_face_link_avatar_for(NB_EXPR_LOVING) == NB_AVATAR_EXPR_LOVING);
    NB_CHECK(nb_face_link_avatar_for(NB_EXPR_EMBARRASSED) == NB_AVATAR_EXPR_EMBARRASSED);
    NB_CHECK(!nb_face_link_show(NULL, NB_EXPR_HAPPY));
}

int main(void) {
    NB_RUN(test_command_holds_face_for_its_whole_duration);
    NB_RUN(test_command_outlives_idle_and_returns_after_touch);
    NB_RUN(test_short_reaction_finishes_its_gesture);
    NB_RUN(test_loving_holds_while_petting_and_idle_clears_it);
    NB_RUN(test_new_winner_replaces_held_expression_at_once);
    NB_RUN(test_state_expressions_switch_without_waiting);
    NB_RUN(test_every_expression_maps_to_an_animation);
    NB_TEST_MAIN_END();
}
