#include <math.h>
#include <string.h>

#include "nb_head_gesture.h"
#include "nb_test.h"

// The bench servos: center 512, provisional limits, 3.2 counts a degree.
static nb_head_geometry_t geometry(void) {
    nb_head_geometry_t g = {512, 512, 410, 614, 461, 563, 1, 1, 3.2f};
    return g;
}

static bool inside(const nb_head_geometry_t *g, const nb_head_plan_t *p) {
    for (int i = 0; i < p->count; i++) {
        if (p->points[i].pan < g->pan_min || p->points[i].pan > g->pan_max ||
            p->points[i].tilt < g->tilt_min || p->points[i].tilt > g->tilt_max) {
            return false;
        }
    }
    return true;
}

static void test_look_goes_to_the_angle_in_one_move(void) {
    const nb_head_geometry_t g = geometry();
    nb_head_plan_t p;
    NB_CHECK(nb_head_plan(&g, NB_HEAD_LOOK, 20.0f, -10.0f, 1, 512, 512, &p));
    NB_CHECK_EQ_INT(1, p.count);
    NB_CHECK_EQ_INT(576, p.points[0].pan);   // 512 + 20 x 3.2
    NB_CHECK_EQ_INT(480, p.points[0].tilt);  // 512 - 10 x 3.2
    NB_CHECK(!p.clamped);
    NB_CHECK_EQ_INT(250, p.points[0].time_ms);  // 20 deg x 12 ms = 240, at least 250
    NB_CHECK(fabsf(nb_head_yaw_deg(&g, p.points[0].pan) - 20.0f) < 0.2f);
    NB_CHECK(fabsf(nb_head_pitch_deg(&g, p.points[0].tilt) + 10.0f) < 0.2f);

    // Mounted the other way round, the same angle turns the other way.
    nb_head_geometry_t flipped = g;
    flipped.pan_sign = -1;
    nb_head_plan(&flipped, NB_HEAD_LOOK, 20.0f, 0.0f, 1, 512, 512, &p);
    NB_CHECK_EQ_INT(448, p.points[0].pan);
}

static void test_a_look_too_far_stops_at_the_limit(void) {
    const nb_head_geometry_t g = geometry();
    nb_head_plan_t p;
    NB_CHECK(nb_head_plan(&g, NB_HEAD_LOOK, 90.0f, 40.0f, 1, 512, 512, &p));
    NB_CHECK(p.clamped);
    NB_CHECK_EQ_INT(614, p.points[0].pan);
    NB_CHECK_EQ_INT(563, p.points[0].tilt);
    NB_CHECK(p.points[0].time_ms <= 1200);
    NB_CHECK(nb_head_plan(&g, NB_HEAD_CENTER, 0.0f, 0.0f, 1, 614, 563, &p));
    NB_CHECK_EQ_INT(512, p.points[0].pan);
    NB_CHECK_EQ_INT(512, p.points[0].tilt);
}

static void test_nod_and_shake_end_where_they_began(void) {
    const nb_head_geometry_t g = geometry();
    nb_head_plan_t p;
    NB_CHECK(nb_head_plan(&g, NB_HEAD_NOD, 0.0f, 0.0f, 2, 530, 520, &p));
    NB_CHECK_EQ_INT(4, p.count);
    NB_CHECK_EQ_INT(530, p.points[p.count - 1].pan);
    NB_CHECK_EQ_INT(520, p.points[p.count - 1].tilt);
    NB_CHECK(p.points[0].tilt < 520);  // down first
    for (int i = 0; i < p.count; i++) {
        NB_CHECK_EQ_INT(530, p.points[i].pan);  // a nod never turns
    }
    NB_CHECK(inside(&g, &p));

    NB_CHECK(nb_head_plan(&g, NB_HEAD_SHAKE, 0.0f, 0.0f, 3, 512, 500, &p));
    NB_CHECK_EQ_INT(512, p.points[p.count - 1].pan);
    bool left = false, right = false;
    for (int i = 0; i < p.count; i++) {
        NB_CHECK_EQ_INT(500, p.points[i].tilt);  // a shake never nods
        left |= p.points[i].pan > 512;
        right |= p.points[i].pan < 512;
    }
    NB_CHECK(left && right);
    NB_CHECK(inside(&g, &p));
}

static void test_near_an_edge_a_gesture_gets_smaller(void) {
    const nb_head_geometry_t g = geometry();
    nb_head_plan_t p;
    NB_CHECK(nb_head_plan(&g, NB_HEAD_NOD, 0.0f, 0.0f, 1, 512, 465, &p));  // nearly fully down
    NB_CHECK(p.clamped);
    NB_CHECK_EQ_INT(461, p.points[0].tilt);
    NB_CHECK(inside(&g, &p));
    // Starting outside the limits (turned by hand) it starts from the edge.
    NB_CHECK(nb_head_plan(&g, NB_HEAD_SHAKE, 0.0f, 0.0f, 1, 300, 512, &p));
    NB_CHECK(inside(&g, &p));
    NB_CHECK_EQ_INT(410, p.points[p.count - 1].pan);
}

static void test_repeats_are_bounded_and_bad_input_refused(void) {
    const nb_head_geometry_t g = geometry();
    nb_head_plan_t p;
    NB_CHECK(nb_head_plan(&g, NB_HEAD_SHAKE, 0.0f, 0.0f, 50, 512, 512, &p));
    NB_CHECK(p.count <= NB_HEAD_WAYPOINTS_MAX);
    uint32_t sum = 0;
    for (int i = 0; i < p.count; i++) {
        sum += p.points[i].time_ms;
    }
    NB_CHECK_EQ_INT(sum, p.total_ms);
    NB_CHECK(nb_head_plan(&g, NB_HEAD_NOD, 0.0f, 0.0f, 0, 512, 512, &p));
    NB_CHECK_EQ_INT(2, p.count);  // at least once

    NB_CHECK(!nb_head_plan(&g, NB_HEAD_LOOK, NAN, 0.0f, 1, 512, 512, &p));
    NB_CHECK(!nb_head_plan(&g, (nb_head_move_t)9, 0.0f, 0.0f, 1, 512, 512, &p));
    nb_head_geometry_t bad = g;
    bad.counts_per_deg = 0.0f;
    NB_CHECK(!nb_head_plan(&bad, NB_HEAD_LOOK, 0.0f, 0.0f, 1, 512, 512, &p));
    NB_CHECK(!nb_head_plan(NULL, NB_HEAD_LOOK, 0.0f, 0.0f, 1, 512, 512, &p));

    nb_head_move_t m;
    NB_CHECK(nb_head_move_from_name("shake", &m) && m == NB_HEAD_SHAKE);
    NB_CHECK(!nb_head_move_from_name("dance", &m));
    NB_CHECK_EQ_STR("nod", nb_head_move_name(NB_HEAD_NOD));
}

int main(void) {
    NB_RUN(test_look_goes_to_the_angle_in_one_move);
    NB_RUN(test_a_look_too_far_stops_at_the_limit);
    NB_RUN(test_nod_and_shake_end_where_they_began);
    NB_RUN(test_near_an_edge_a_gesture_gets_smaller);
    NB_RUN(test_repeats_are_bounded_and_bad_input_refused);
    NB_TEST_MAIN_END();
}
