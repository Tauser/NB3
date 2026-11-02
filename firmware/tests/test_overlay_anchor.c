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

static const nb_overlay_rect_t kBars = {-35.5f, 66.0f, 35.5f, 98.0f};

static void test_clamp_leaves_an_extent_that_fits(void) {
    nb_overlay_anchor_t a = {3.0f, -4.0f, 0.8f};
    nb_overlay_clamp_anchor(&kBars, 320.0f, 240.0f, &a);
    NB_CHECK(fabsf(a.dx - 3.0f) < 1e-4f && fabsf(a.dy + 4.0f) < 1e-4f);
}

static void test_clamp_pulls_bars_back_from_the_bottom(void) {
    // Eyes low and looking down: the bars would end at y = 120 + 60 + 98*.8.
    nb_overlay_anchor_t a = {0.0f, 60.0f, 0.8f};
    nb_overlay_clamp_anchor(&kBars, 320.0f, 240.0f, &a);
    NB_CHECK(fabsf((120.0f + a.dy + kBars.y1 * a.scale) - 240.0f) < 1e-3f);
    NB_CHECK(fabsf(a.dx) < 1e-4f);
}

static void test_clamp_pushes_in_from_the_left_and_top(void) {
    const nb_overlay_rect_t zs = {61.0f, -119.0f, 117.0f, -49.0f};
    nb_overlay_anchor_t a = {-200.0f, -50.0f, 1.0f};
    nb_overlay_clamp_anchor(&zs, 320.0f, 240.0f, &a);
    NB_CHECK(120.0f + a.dy + zs.y0 * a.scale >= -1e-3f);
    NB_CHECK(160.0f + a.dx + zs.x0 * a.scale >= -1e-3f);
}

static void test_clamp_keeps_the_top_left_of_an_extent_bigger_than_the_screen(void) {
    const nb_overlay_rect_t huge = {-300.0f, -300.0f, 300.0f, 300.0f};
    nb_overlay_anchor_t a = {0.0f, 0.0f, 1.0f};
    nb_overlay_clamp_anchor(&huge, 320.0f, 240.0f, &a);
    NB_CHECK(fabsf(160.0f + a.dx + huge.x0) < 1e-3f);
    NB_CHECK(fabsf(120.0f + a.dy + huge.y0) < 1e-3f);
}

static void test_banner_hides_what_it_reaches(void) {
    nb_overlay_anchor_t a = {0.0f, 0.0f, 0.8f};
    // Bars end at 120 + 98*.8 = 198.4.
    NB_CHECK(!nb_overlay_hidden_by_banner(&kBars, &a, 240.0f, 240.0f));  // no banner
    NB_CHECK(!nb_overlay_hidden_by_banner(&kBars, &a, 240.0f, 200.0f));
    NB_CHECK(nb_overlay_hidden_by_banner(&kBars, &a, 240.0f, 198.0f));
    NB_CHECK(nb_overlay_hidden_by_banner(&kBars, &a, 240.0f, 150.0f));
}

int main(void) {
    NB_RUN(test_clamp_leaves_an_extent_that_fits);
    NB_RUN(test_clamp_pulls_bars_back_from_the_bottom);
    NB_RUN(test_clamp_pushes_in_from_the_left_and_top);
    NB_RUN(test_clamp_keeps_the_top_left_of_an_extent_bigger_than_the_screen);
    NB_RUN(test_banner_hides_what_it_reaches);
    NB_RUN(test_rest_pose_leaves_overlay_in_place);
    NB_RUN(test_overlay_moves_with_the_eyes);
    NB_RUN(test_head_turn_uses_the_eye_midpoint);
    NB_RUN(test_face_offset_and_scale);
    NB_TEST_MAIN_END();
}
