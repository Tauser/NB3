#include <math.h>
#include <string.h>

#include "nb_overlay_anchor.h"
#include "nb_test.h"

static nb_avatar_pose_t rest_pose(void) {
    nb_avatar_pose_t p;
    memset(&p, 0, sizeof p);
    p.face_scale = 1.0f;
    p.left_eye.x = -200.0f;
    p.right_eye.x = 200.0f;
    return p;
}

static void test_rest_pose_leaves_overlay_in_place(void) {
    nb_avatar_pose_t p = rest_pose();
    nb_overlay_anchor_t a;
    nb_overlay_anchor_from_pose(&p, 0.3f, &a);
    NB_CHECK(fabsf(a.dx) < 1e-4f && fabsf(a.dy) < 1e-4f);
    NB_CHECK(fabsf(a.scale - NB_OVERLAY_SIZE) < 1e-4f);
}

static void test_overlay_moves_with_the_eyes(void) {
    nb_avatar_pose_t p = rest_pose();
    p.left_eye.x += 50.0f;
    p.right_eye.x += 50.0f;
    p.left_eye.y -= 20.0f;
    p.right_eye.y -= 20.0f;
    nb_overlay_anchor_t a;
    nb_overlay_anchor_from_pose(&p, 0.3f, &a);
    NB_CHECK(fabsf(a.dx - 15.0f) < 1e-3f);
    NB_CHECK(fabsf(a.dy + 6.0f) < 1e-3f);
}

static void test_head_turn_uses_the_eye_midpoint(void) {
    nb_avatar_pose_t p = rest_pose();
    p.left_eye.x = -120.0f;  // far eye pulled in, near eye pushed out
    p.right_eye.x = 260.0f;
    nb_overlay_anchor_t a;
    nb_overlay_anchor_from_pose(&p, 0.3f, &a);
    NB_CHECK(fabsf(a.dx - 0.5f * (260.0f - 120.0f) * 0.3f) < 1e-3f);
}

static void test_face_offset_and_scale(void) {
    nb_avatar_pose_t p = rest_pose();
    p.face_x = 40.0f;
    p.face_scale = 1.2f;
    nb_overlay_anchor_t a;
    nb_overlay_anchor_from_pose(&p, 0.3f, &a);
    NB_CHECK(fabsf(a.dx - 40.0f * 0.3f * 1.2f) < 1e-3f);
    NB_CHECK(a.scale > NB_OVERLAY_SIZE && a.scale < NB_OVERLAY_SIZE * 1.2f);
}

int main(void) {
    NB_RUN(test_rest_pose_leaves_overlay_in_place);
    NB_RUN(test_overlay_moves_with_the_eyes);
    NB_RUN(test_head_turn_uses_the_eye_midpoint);
    NB_RUN(test_face_offset_and_scale);
    NB_TEST_MAIN_END();
}
