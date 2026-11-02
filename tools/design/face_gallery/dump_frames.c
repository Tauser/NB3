// Samples the real avatar core with a fixed seed and a fixed clock and prints
// JSON for the face gallery and for the reference poses.
//
//   dump_frames              all 15 intentions, at rest and at the four look corners
//   dump_frames --rest       the same without the look corners (the stored reference)
//   dump_frames --contract   where the overlay channels fall against the eyes
//
// Built by build_gallery.py with the host compiler; nothing here touches
// ESP-IDF. The projection follows FaceDisplay's Painter::Eye.
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nb_avatar.h"
#include "nb_overlay_anchor.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define DESIGN_SCALE 0.30f  // kDesignScale in face/face_display.cc
#define TICK_MS 33u
#define SEED 1234u

#define REST_RELEASE_MS 3500u
#define REST_TOTAL_MS 5200u
#define LOOK_RELEASE_MS 1100u
#define LOOK_TOTAL_MS 1500u
#define CONTRACT_HOLD_MS 6000u

typedef struct {
    const char *name;
    nb_avatar_look_t look;
} look_case_t;

// The extremes of core/nb_gaze: head turn, eye slide and roll at the limits.
static const look_case_t kLooks[] = {
    {"rest", {0, 0, 0, 0, 0}},
    {"up_left", {-27, -26, -5, -8, -20}},
    {"up_right", {27, -26, 5, 8, -20}},
    {"down_left", {-27, 26, -5, -8, 20}},
    {"down_right", {27, 26, 5, 8, 20}},
};
#define LOOK_COUNT ((int)(sizeof(kLooks) / sizeof(kLooks[0])))

static void start(nb_avatar_core_state_t *st, nb_avatar_expression_t expr,
                  const look_case_t *look) {
    nb_avatar_core_init(st, SEED);
    if (look->name[0] != 'r') {
        nb_avatar_core_set_look(st, &look->look, true);
    }
    if (expr != NB_AVATAR_EXPR_IDLE) {
        nb_avatar_core_hold(st, expr);
    }
}

static void print_eye(const nb_avatar_eye_pose_t *e) {
    printf("[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]", e->x, e->y, e->width,
           e->height, e->roundness, e->upper_lid, e->upper_lid_tilt, e->lower_lid, e->crescent,
           e->angle, e->heart);
}

static void print_frame(uint32_t t, const nb_avatar_render_frame_t *f) {
    nb_overlay_anchor_t a;
    nb_overlay_anchor_from_pose(&f->pose, DESIGN_SCALE, &a);
    printf("[%u,%.3f,%d,%.4f,%.2f,%.2f,", (unsigned)t, f->blink_scale, f->dizzy_overlay ? 1 : 0,
           f->pose.face_scale, f->pose.face_x, f->pose.face_y);
    print_eye(&f->pose.left_eye);
    printf(",");
    print_eye(&f->pose.right_eye);
    printf(",%.2f,%.2f,%.4f]", a.dx, a.dy, a.scale);
}

static void print_scenario(nb_avatar_expression_t expr, const look_case_t *look, bool first) {
    const bool at_rest = look->name[0] == 'r';
    const uint32_t release_ms = at_rest ? REST_RELEASE_MS : LOOK_RELEASE_MS;
    const uint32_t total_ms = at_rest ? REST_TOTAL_MS : LOOK_TOTAL_MS;
    nb_avatar_core_state_t st;
    start(&st, expr, look);
    printf("%s{\"expression\":\"%s\",\"look\":\"%s\",\"release_ms\":%u,\"frames\":[", first ? "" : ",",
           nb_avatar_core_expression_name(expr), look->name, (unsigned)release_ms);
    bool released = false;
    for (uint32_t t = TICK_MS; t <= total_ms; t += TICK_MS) {
        if (!released && t > release_ms && expr != NB_AVATAR_EXPR_IDLE) {
            nb_avatar_core_release(&st, NB_AVATAR_EXPR_IDLE);
            released = true;
        }
        nb_avatar_core_tick(&st, TICK_MS);
        nb_avatar_render_frame_t f;
        nb_avatar_core_get_frame(&st, &f);
        printf("%s", t == TICK_MS ? "\n" : ",\n");
        print_frame(t, &f);
    }
    printf("]}");
}

static int dump(bool with_looks) {
    printf("{\"design_scale\":%.2f,\"tick_ms\":%u,\"seed\":%u,\"screen\":[%d,%d],\"scenarios\":[",
           DESIGN_SCALE, (unsigned)TICK_MS, (unsigned)SEED, SCREEN_W, SCREEN_H);
    bool first = true;
    for (int e = 0; e < NB_AVATAR_EXPR_COUNT; e++) {
        for (int l = 0; l < LOOK_COUNT; l++) {
            if (!with_looks && l > 0) {
                break;
            }
            print_scenario((nb_avatar_expression_t)e, &kLooks[l], first);
            first = false;
            printf("\n");
        }
    }
    printf("]}\n");
    return 0;
}

// ---- Contract: channel rectangles against the eyes -------------------------

typedef struct {
    float x0, y0, x1, y1;
} rect_t;

// The overlay layout, in screen pixels from the eyes' rest centre. Keep in step
// with the constants in face/face_display.cc (test_face_gallery.py checks it).
#define BARS_LEFT (-35.5f)
#define BARS_STEP 11.0f
#define BARS_CENTRE_Y 82.0f
#define BARS_MAX_HEIGHT 32.0f
#define BARS_COUNT 7
#define HAND_LEFT (-32.0f)
#define HAND_TOP 47.0f
#define HAND_W 54.0f
#define HAND_H 50.0f
#define BUBBLE_CX 108.0f
#define BUBBLE_CY (-80.0f)
#define HEARTS_Y (-60.0f)
#define HEARTS_RISE 36.0f
#define Z_START_X 70.0f
#define Z_START_Y (-58.0f)
#define Z_DRIFT_X 38.0f
#define Z_DRIFT_Y (-52.0f)
// Channel T is fixed on the screen.
static const rect_t kIcon = {282.0f, 8.0f, 314.0f, 40.0f};

static bool touches(const rect_t *r, const nb_avatar_eye_pose_t *e, const nb_avatar_pose_t *p) {
    const float s = DESIGN_SCALE * p->face_scale;
    const float ex = SCREEN_W * 0.5f + (p->face_x + e->x) * s;
    const float ey = SCREEN_H * 0.5f + (p->face_y + e->y) * s;
    const float w = e->width * s * 0.5f;
    const float h = e->height * s * 0.5f;
    return r->x0 < ex + w && r->x1 > ex - w && r->y0 < ey + h && r->y1 > ey - h;
}

typedef struct {
    int frames;
    int hits;
    float min_x, min_y, max_x, max_y;
} stat_t;

static void stat_init(stat_t *s) {
    s->frames = 0;
    s->hits = 0;
    s->min_x = s->min_y = 1e9f;
    s->max_x = s->max_y = -1e9f;
}

static void stat_add(stat_t *s, const rect_t *r, const nb_avatar_pose_t *p) {
    s->frames++;
    s->hits += touches(r, &p->left_eye, p) || touches(r, &p->right_eye, p);
    if (r->x0 < s->min_x) s->min_x = r->x0;
    if (r->y0 < s->min_y) s->min_y = r->y0;
    if (r->x1 > s->max_x) s->max_x = r->x1;
    if (r->y1 > s->max_y) s->max_y = r->y1;
}

static void print_stat(const char *name, const stat_t *s, bool last) {
    printf("\"%s\":{\"hit_pct\":%d,\"min_x\":%.0f,\"min_y\":%.0f,\"max_x\":%.0f,\"max_y\":%.0f}%s",
           name, s->frames ? 100 * s->hits / s->frames : 0, s->min_x, s->min_y, s->max_x, s->max_y,
           last ? "" : ",");
}

static void contract_scenario(nb_avatar_expression_t expr, const look_case_t *look, bool first) {
    nb_avatar_core_state_t st;
    start(&st, expr, look);
    stat_t bars, hand, bubble, hearts, zs, icon;
    stat_init(&bars);
    stat_init(&hand);
    stat_init(&bubble);
    stat_init(&hearts);
    stat_init(&zs);
    stat_init(&icon);
    float eye_x0 = 1e9f, eye_y0 = 1e9f, eye_x1 = -1e9f, eye_y1 = -1e9f;
    for (uint32_t t = 0; t < CONTRACT_HOLD_MS; t += TICK_MS) {
        nb_avatar_core_tick(&st, TICK_MS);
        nb_avatar_render_frame_t f;
        nb_avatar_core_get_frame(&st, &f);
        const nb_avatar_pose_t *p = &f.pose;
        nb_overlay_anchor_t a;
        nb_overlay_anchor_from_pose(p, DESIGN_SCALE, &a);
        const float ox = SCREEN_W * 0.5f + a.dx;
        const float oy = SCREEN_H * 0.5f + a.dy;
        const float s0 = a.scale;
        const float bars_w = (BARS_COUNT - 1) * BARS_STEP + 5.0f;
        const rect_t r_bars = {ox + BARS_LEFT * s0, oy + (BARS_CENTRE_Y - BARS_MAX_HEIGHT * 0.5f) * s0,
                               ox + (BARS_LEFT + bars_w) * s0,
                               oy + (BARS_CENTRE_Y + BARS_MAX_HEIGHT * 0.5f) * s0};
        const rect_t r_hand = {ox + HAND_LEFT * s0, oy + HAND_TOP * s0, ox + (HAND_LEFT + HAND_W) * s0,
                               oy + (HAND_TOP + HAND_H) * s0};
        const rect_t r_bubble = {ox + (BUBBLE_CX - 36.0f) * s0, oy + (BUBBLE_CY - 22.0f) * s0,
                                 ox + (BUBBLE_CX + 36.0f) * s0, oy + (BUBBLE_CY + 36.0f) * s0};
        const rect_t r_hearts = {ox - 20.0f * s0, oy + (HEARTS_Y - HEARTS_RISE - 14.0f) * s0,
                                 ox + 22.0f * s0, oy + (HEARTS_Y + 20.0f) * s0};
        const rect_t r_zs = {ox + (Z_START_X - 9.0f) * s0, oy + (Z_START_Y + Z_DRIFT_Y - 9.0f) * s0,
                             ox + (Z_START_X + Z_DRIFT_X + 9.0f) * s0, oy + (Z_START_Y + 9.0f) * s0};
        stat_add(&bars, &r_bars, p);
        stat_add(&hand, &r_hand, p);
        stat_add(&bubble, &r_bubble, p);
        stat_add(&hearts, &r_hearts, p);
        stat_add(&zs, &r_zs, p);
        stat_add(&icon, &kIcon, p);
        for (int i = 0; i < 2; i++) {
            const nb_avatar_eye_pose_t *e = i == 0 ? &p->left_eye : &p->right_eye;
            const float s = DESIGN_SCALE * p->face_scale;
            const float ex = SCREEN_W * 0.5f + (p->face_x + e->x) * s;
            const float ey = SCREEN_H * 0.5f + (p->face_y + e->y) * s;
            if (ex - e->width * s * 0.5f < eye_x0) eye_x0 = ex - e->width * s * 0.5f;
            if (ex + e->width * s * 0.5f > eye_x1) eye_x1 = ex + e->width * s * 0.5f;
            if (ey - e->height * s * 0.5f < eye_y0) eye_y0 = ey - e->height * s * 0.5f;
            if (ey + e->height * s * 0.5f > eye_y1) eye_y1 = ey + e->height * s * 0.5f;
        }
    }
    printf("%s{\"expression\":\"%s\",\"look\":\"%s\",\"eyes\":{\"min_x\":%.0f,\"min_y\":%.0f,"
           "\"max_x\":%.0f,\"max_y\":%.0f},",
           first ? "" : ",\n", nb_avatar_core_expression_name(expr), look->name, eye_x0, eye_y0,
           eye_x1, eye_y1);
    print_stat("bars", &bars, false);
    print_stat("hand", &hand, false);
    print_stat("bubble", &bubble, false);
    print_stat("hearts", &hearts, false);
    print_stat("zs", &zs, false);
    print_stat("icon", &icon, true);
    printf("}");
}

static int contract(void) {
    printf("{\"screen\":[%d,%d],\"scenarios\":[\n", SCREEN_W, SCREEN_H);
    bool first = true;
    for (int e = 0; e < NB_AVATAR_EXPR_COUNT; e++) {
        for (int l = 0; l < LOOK_COUNT; l++) {
            contract_scenario((nb_avatar_expression_t)e, &kLooks[l], first);
            first = false;
        }
    }
    printf("\n]}\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--contract") == 0) {
        return contract();
    }
    return dump(!(argc > 1 && strcmp(argv[1], "--rest") == 0));
}
