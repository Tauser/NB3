#include <math.h>
#include <string.h>

#include "nb_head_follow.h"
#include "nb_test.h"

static nb_head_follow_input_t awake(float face_yaw, float face_pitch) {
    nb_head_follow_input_t in;
    memset(&in, 0, sizeof(in));
    in.can_move = true;
    in.mode = NB_FOLLOW_AWAKE;
    in.face_own = true;
    in.face_yaw = face_yaw;
    in.face_pitch = face_pitch;
    return in;
}

static nb_head_follow_t follower(void) {
    nb_head_follow_t f;
    const nb_head_follow_cfg_t cfg = nb_head_follow_default_cfg();
    nb_head_follow_init(&f, &cfg);
    return f;
}

static void test_the_head_follows_the_eyes_a_fraction_of_the_way(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(20.0f, 10.0f);  // eyes to its left and down
    nb_head_follow_update(&f, &in, 0, &out);
    NB_CHECK(out.move && out.kind == NB_HEAD_LOOK);
    NB_CHECK(fabsf(out.yaw - 8.0f) < 1e-4f);    // 0.4 of the way
    NB_CHECK(fabsf(out.pitch + 3.0f) < 1e-4f);  // down on screen = negative for the head
    NB_CHECK(out.min_time_ms >= 700);
}

static void test_small_or_quick_changes_do_not_move_it(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(10.0f, 0.0f);
    nb_head_follow_update(&f, &in, 0, &out);
    NB_CHECK(out.move);
    in.face_yaw = 15.0f;  // head 6 instead of 4: under the 4 degree change
    nb_head_follow_update(&f, &in, 5000, &out);
    NB_CHECK(!out.move);
    in.face_yaw = 30.0f;  // worth it, but too soon
    nb_head_follow_update(&f, &in, 1000 + 5000, &out);
    NB_CHECK(out.move);  // 6 s after the last move: allowed
    in.face_yaw = -30.0f;
    nb_head_follow_update(&f, &in, 6500, &out);
    NB_CHECK(!out.move);  // 0.5 s later
    nb_head_follow_update(&f, &in, 7500, &out);
    NB_CHECK(out.move && out.yaw < 0.0f);
}

static void test_looking_at_the_user_brings_the_head_to_center(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(25.0f, 0.0f);
    nb_head_follow_update(&f, &in, 0, &out);
    in.face_own = false;  // a reaction: the eyes come back to the user
    nb_head_follow_update(&f, &in, 2000, &out);
    NB_CHECK(out.move && out.yaw == 0.0f && out.pitch == 0.0f);
    in = awake(25.0f, 0.0f);
    in.mode = NB_FOLLOW_TALKING;  // talking: looks at the user whatever the eyes did
    nb_head_follow_update(&f, &in, 4000, &out);
    NB_CHECK(!out.move);  // already centered
}

static void test_the_ai_comes_first(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(25.0f, 0.0f);
    nb_head_follow_on_ai(&f, 0);
    nb_head_follow_update(&f, &in, 100, &out);
    NB_CHECK(!out.move);
    nb_head_follow_on_tap(&f, 200);
    nb_head_follow_update(&f, &in, 5999, &out);
    NB_CHECK(!out.move);  // nor a tap nod in the meantime
    nb_head_follow_update(&f, &in, 6000, &out);
    NB_CHECK(out.move && out.kind == NB_HEAD_LOOK);  // the dropped tap stays dropped
}

static void test_a_tap_nods_once_and_petting_leans_in(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(0.0f, 0.0f);
    nb_head_follow_update(&f, &in, 0, &out);
    nb_head_follow_on_tap(&f, 100);
    nb_head_follow_update(&f, &in, 120, &out);
    NB_CHECK(out.move && out.kind == NB_HEAD_NOD && out.repeats == 1);
    nb_head_follow_on_tap(&f, 500);  // too soon after the last: ignored
    nb_head_follow_update(&f, &in, 520, &out);
    NB_CHECK(!out.move);

    in.petting = true;
    nb_head_follow_update(&f, &in, 3000, &out);
    NB_CHECK(out.move && out.kind == NB_HEAD_LOOK && out.pitch < 0.0f && out.yaw == 0.0f);
    in.petting = false;
    nb_head_follow_update(&f, &in, 5000, &out);
    NB_CHECK(out.move && out.pitch == 0.0f);  // back up once the hand leaves
}

static void test_asleep_it_droops_and_ignores_taps(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(20.0f, 0.0f);
    in.mode = NB_FOLLOW_ASLEEP;
    nb_head_follow_on_tap(&f, 0);
    nb_head_follow_update(&f, &in, 10, &out);
    NB_CHECK(out.move && out.kind == NB_HEAD_LOOK);
    NB_CHECK(out.yaw == 0.0f && out.pitch < 0.0f);
    nb_head_follow_update(&f, &in, 5000, &out);
    NB_CHECK(!out.move);  // stays put
}

static void test_nothing_while_the_safety_says_no(void) {
    nb_head_follow_t f = follower();
    nb_head_follow_out_t out;
    nb_head_follow_input_t in = awake(25.0f, 0.0f);
    in.can_move = false;
    nb_head_follow_on_tap(&f, 0);
    nb_head_follow_update(&f, &in, 10, &out);
    NB_CHECK(!out.move);
    in.can_move = true;
    nb_head_follow_update(&f, &in, 20, &out);
    NB_CHECK(out.move && out.kind == NB_HEAD_LOOK);  // not the stale tap
}

int main(void) {
    NB_RUN(test_the_head_follows_the_eyes_a_fraction_of_the_way);
    NB_RUN(test_small_or_quick_changes_do_not_move_it);
    NB_RUN(test_looking_at_the_user_brings_the_head_to_center);
    NB_RUN(test_the_ai_comes_first);
    NB_RUN(test_a_tap_nods_once_and_petting_leans_in);
    NB_RUN(test_asleep_it_droops_and_ignores_taps);
    NB_RUN(test_nothing_while_the_safety_says_no);
    NB_TEST_MAIN_END();
}
