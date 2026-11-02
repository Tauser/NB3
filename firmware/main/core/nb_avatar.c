#include "nb_avatar.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define NB_AVATAR_MAX_ADVANCES 64u
#define NB_AVATAR_PI 3.14159265358979323846f
/* How far the top cut comes down on a shut eye: a thin lower arc is left. */
#define NB_AVATAR_SHUT (-0.93f)
/* Share of an expression's own motion kept while it is held: enough to stay
 * alive, calm enough that an ANGRY shake or HAPPY bounce can last a minute. */
#define NB_AVATAR_HOLD_MOTION 0.3f

/* Every expression is one of six geometric families, drawn by a generator from
 * a handful of scalars, plus a recipe that says how strongly, how fast and with
 * what motion. There are no stored poses: the entrance (an optional
 * anticipation, then the peak), the settled pose and the swaying are all
 * derived from the recipe. */
typedef enum {
    FAMILY_PRESENCE = 0,  // open capsules: rest, attention, thought, surprise, dizziness
    FAMILY_JOY,           // capsules and crescents: happy, excited, loving, content
    FAMILY_CURIOSITY,     // an uneven pair: curious, confused
    FAMILY_CONTRARY,      // half-moons slanted toward the middle: angry
    FAMILY_SADNESS,       // half-moons drooping outward, looking down: sad, embarrassed
    FAMILY_SLEEPINESS,    // capsules sinking into upside-down arcs
} nb_avatar_family_t;

typedef enum {
    BLINK_NONE = 0,
    BLINK_CALM,
    BLINK_ATTENTIVE,
    BLINK_QUICK,
    BLINK_SLOW,
    BLINK_PROFILE_COUNT,
} nb_avatar_blink_profile_t;

typedef struct {
    bool enabled;
    uint16_t initial_delay_ms;
    uint16_t min_interval_ms;
    uint16_t max_interval_ms;
    uint16_t close_ms;
    uint16_t open_ms;
} nb_avatar_blink_t;

/* What a family generator reads. The meaning of `a` and `v` is the family's;
 * the rest is common: eye size, how far the eyes sit from the middle, where
 * they look, the head's lift and turn. */
typedef struct {
    float a;       // main intensity (see each generator)
    float v;       // variant: size imbalance, or the shy lid of SADNESS
    float dw;      // width change, as a fraction of the family's
    float dh;      // height change
    float spread;  // added to the distance of each eye from the middle
    float lx;      // eyes slide sideways
    float ly;      // eyes slide down
    float fy;      // face lift (positive goes down)
    float zoom;    // face scale above 1
    float pitch;   // head, degrees
    float yaw;
    float roll;
    float lid;     // half-moon lid of the right eye (CURIOSITY)
    float heart;   // the eyes are hearts (JOY)
} nb_avatar_shape_t;

typedef struct {
    const char *name;
    nb_avatar_family_t family;
    bool persistent;  // base expressions loop; the others play once
    bool springy;     // the entrance overshoots
    nb_avatar_shape_t peak;
    float settle;       // share of a, lx and ly kept once settled
    float ant_amount;   // intensity of the anticipation, as a share of the peak
    float antic_open;   // >0: the anticipation is a plain capsule this tall (share of rest)
    float roll_sway;    // degrees: +, - and + again across anticipation, peak and settle
    float variant_sway;
    uint16_t ant_ms, ant_hold_ms, peak_ms, peak_hold_ms, settle_ms, settle_hold_ms;
    nb_avatar_blink_profile_t blink;
    float bob_amp, bob_w, bob_offset;  // face, vertical
    float shake_amp, shake_w;          // face, horizontal
    float drift_amp, drift_w;          // eyes inside the face
    float pulse_amp, pulse_w;          // face scale
    uint16_t blend_in_ms, blend_out_ms;
} nb_avatar_recipe_t;

#define REST_X 225.0f
#define REST_Y (-15.0f)
#define REST_W 155.0f
#define REST_H 360.0f
#define FACE_Y (-8.0f)
/* Overshoot is calmer once settled: the lift and the zoom keep this much. */
#define SETTLE_LIFT 0.75f
#define SETTLE_ZOOM 0.45f
/* The anticipation squashes the eyes into plain capsules this much narrower. */
#define SQUASH_WIDTH 0.88f
#define SQUASH_DROP 0.12f
#define SWAY_SETTLE 0.6f

static nb_avatar_eye_pose_t make_eye(float x, float y, float w, float h, float lid, float tilt,
                                     float crescent)
{
    nb_avatar_eye_pose_t eye;
    memset(&eye, 0, sizeof(eye));
    eye.x = x;
    eye.y = y;
    eye.width = w;
    eye.height = h;
    eye.roundness = 1.0f;
    eye.upper_lid = lid;
    eye.upper_lid_tilt = tilt;
    eye.brow_y = -230.0f;
    eye.crescent = crescent;
    return eye;
}

/* The pose both eyes share: size imbalance v makes the left one bigger.
 * `lid_left` is the left lid alone, `lid` the right one's. */
static nb_avatar_pose_t assemble(const nb_avatar_shape_t *s, float base_w, float base_h,
                                 float base_y, float lid_left, float lid, float tilt,
                                 float crescent, float imbalance_drop, float face_fy)
{
    const float wl = base_w * (1.0f + s->dw) * (1.0f + s->v);
    const float wr = base_w * (1.0f + s->dw) * (1.0f - s->v);
    const float hl = base_h * (1.0f + s->dh) * (1.0f + s->v);
    const float hr = base_h * (1.0f + s->dh) * (1.0f - s->v);
    const float x = REST_X + s->spread;
    /* The bigger eye rides higher, the smaller one lower. */
    const float drop = imbalance_drop * (hl - hr) * 0.5f;
    nb_avatar_pose_t pose;
    pose.left_eye = make_eye(-x + s->lx, base_y + s->ly - drop, wl, hl, lid_left, tilt, crescent);
    pose.right_eye = make_eye(x + s->lx, base_y + s->ly + drop, wr, hr, lid, -tilt, crescent);
    pose.left_eye.heart = s->heart;
    pose.right_eye.heart = s->heart;
    pose.face_scale = 1.0f + s->zoom;
    pose.face_x = 0.0f;
    pose.face_y = FACE_Y + s->fy + face_fy;
    pose.head_yaw = s->yaw;
    pose.head_pitch = s->pitch;
    pose.head_roll = s->roll;
    return pose;
}

/* a: how far the capsules open and widen (0 rest .. 1 a round 255 circle). */
static nb_avatar_pose_t gen_presence(const nb_avatar_shape_t *s)
{
    return assemble(s, REST_W + 100.0f * s->a, REST_H - 105.0f * s->a, REST_Y, 0, 0, 0, 0, 0, 0);
}

/* a: how far the capsule closes into an arc (0 capsule .. 1 a sliver). */
static nb_avatar_pose_t gen_joy(const nb_avatar_shape_t *s)
{
    return assemble(s, REST_W, REST_H, REST_Y, 0, 0, 0, s->a, 0, 0);
}

/* v: the size imbalance of the pair (the bigger eye rides higher); the right
 * eye takes the half-moon lid `lid`. a is unused. */
static nb_avatar_pose_t gen_curiosity(const nb_avatar_shape_t *s)
{
    return assemble(s, 160.0f, 320.0f, -16.0f, 0, s->lid, 0, 0, 0.23f, 0);
}

/* a: how far the half-moons slant toward the middle (the lid drop follows). */
static nb_avatar_pose_t gen_contrary(const nb_avatar_shape_t *s)
{
    return assemble(s, REST_W + 90.0f * s->a, REST_H - 170.0f * s->a, REST_Y, 0.14f * s->a,
                    0.14f * s->a, 0.75f * s->a, 0, 0, 0);
}

/* a: how far the half-moons droop outward. v: 0 sad .. 1 shy (a heavier lid). */
static nb_avatar_pose_t gen_sadness(const nb_avatar_shape_t *s)
{
    const float lid = 0.15f + 0.30f * s->v;
    nb_avatar_shape_t even = *s;
    even.v = 0.0f;
    return assemble(&even, 185.0f, 255.0f, REST_Y, lid, lid, -s->a, 0, 0, 0);
}

/* a: how far the lids have come down over small round eyes (flat-topped
 * half-moons); closing the eyes into arcs is the sleep, not this. */
static nb_avatar_pose_t gen_sleepiness(const nb_avatar_shape_t *s)
{
    const float lid = 0.4f * s->a;
    return assemble(s, 190.0f - 20.0f * s->a, 190.0f - 25.0f * s->a, 18.0f + 12.0f * s->a, lid,
                    lid, 0, 0, 0, 26.0f + 8.0f * s->a);
}

static nb_avatar_pose_t generate(nb_avatar_family_t family, const nb_avatar_shape_t *s)
{
    switch (family) {
    case FAMILY_JOY:
        return gen_joy(s);
    case FAMILY_CURIOSITY:
        return gen_curiosity(s);
    case FAMILY_CONTRARY:
        return gen_contrary(s);
    case FAMILY_SADNESS:
        return gen_sadness(s);
    case FAMILY_SLEEPINESS:
        return gen_sleepiness(s);
    case FAMILY_PRESENCE:
    default:
        return gen_presence(s);
    }
}

#define SHAPE(...) {__VA_ARGS__}
#define MOTION(bob_amp_, bob_w_, bob_off_, shake_amp_, shake_w_, drift_amp_, drift_w_,          \
               pulse_amp_, pulse_w_, in_, out_)                                                    \
    .bob_amp = (bob_amp_), .bob_w = (bob_w_), .bob_offset = (bob_off_),                           \
    .shake_amp = (shake_amp_), .shake_w = (shake_w_), .drift_amp = (drift_amp_),                  \
    .drift_w = (drift_w_), .pulse_amp = (pulse_amp_), .pulse_w = (pulse_w_),                      \
    .blend_in_ms = (in_), .blend_out_ms = (out_)
#define TIMES(a_, ah_, p_, ph_, s_, sh_)                                                          \
    .ant_ms = (a_), .ant_hold_ms = (ah_), .peak_ms = (p_), .peak_hold_ms = (ph_),                 \
    .settle_ms = (s_), .settle_hold_ms = (sh_)

static const nb_avatar_blink_t s_blink[BLINK_PROFILE_COUNT] = {
    [BLINK_NONE] = {false, 0, 0, 0, 0, 0},
    [BLINK_CALM] = {true, 2600, 3400, 6200, 112, 168},
    [BLINK_ATTENTIVE] = {true, 1900, 2800, 5200, 95, 145},
    [BLINK_QUICK] = {true, 1200, 1800, 3600, 72, 112},
    [BLINK_SLOW] = {true, 1800, 3200, 5600, 130, 190},
};

/* The recipes, one per intention. Durations are those of the catalog this one
 * replaced (docs/PLANO-FACE-E-COMPORTAMENTO-2026-11-02.md, section 4). */
static const nb_avatar_recipe_t s_recipes[NB_AVATAR_EXPR_COUNT] = {
    [NB_AVATAR_EXPR_IDLE] = {
        .name = "IDLE", .family = FAMILY_PRESENCE, .persistent = true,
        .peak = SHAPE(.a = 0.0f), .settle = 1.0f, TIMES(0, 0, 260, 0, 0, 0),
        .blink = BLINK_CALM, MOTION(3.2f, 1.15f, 0, 0, 0, 0, 0, .0045f, 1.05f, 180, 240)},
    [NB_AVATAR_EXPR_LISTENING] = {
        .name = "LISTENING", .family = FAMILY_PRESENCE, .persistent = true,
        .peak = SHAPE(.a = 0.4f, .ly = -3.0f, .fy = -1.0f, .pitch = -3.0f), .settle = 0.88f,
        TIMES(0, 0, 300, 1200, 600, 1200), .blink = BLINK_ATTENTIVE,
        MOTION(2.5f, .85f, -1, 1.2f, .65f, 2.4f, .72f, .0035f, .95f, 280, 360)},
    [NB_AVATAR_EXPR_THINKING] = {
        .name = "THINKING", .family = FAMILY_PRESENCE, .persistent = true,
        .peak = SHAPE(.a = 0.05f, .v = 0.03f, .dw = -0.06f, .dh = -0.06f, .lx = 30.0f,
                      .ly = -45.0f, .fy = -2.0f, .pitch = -6.0f, .yaw = 6.0f),
        .settle = 0.9f, TIMES(0, 0, 420, 2200, 900, 2000), .blink = BLINK_CALM,
        MOTION(3, .68f, -2, 2.8f, .52f, 4, .63f, .003f, .72f, 320, 420)},
    [NB_AVATAR_EXPR_HAPPY] = {
        .name = "HAPPY", .family = FAMILY_JOY, .springy = true,
        .peak = SHAPE(.a = 0.78f, .dw = 0.8f, .dh = -0.5f, .spread = -3.0f, .ly = 5.0f,
                      .fy = -4.0f, .zoom = 0.035f),
        .settle = 1.03f, .antic_open = 0.79f, TIMES(115, 0, 225, 900, 200, 420),
        .blink = BLINK_QUICK, MOTION(15, 8, 0, 0, 0, 3.5f, 1.1f, .008f, 2.4f, 180, 240)},
    [NB_AVATAR_EXPR_EXCITED] = {
        .name = "EXCITED", .family = FAMILY_JOY, .springy = true,
        .peak = SHAPE(.dh = 0.14f, .dw = 0.45f, .ly = -19.0f, .fy = -16.0f, .zoom = 0.075f,
                      .pitch = -11.0f),
        .settle = 0.5f, .antic_open = 0.31f, TIMES(90, 25, 155, 180, 160, 700),
        .blink = BLINK_QUICK, MOTION(15, 8.5f, 0, 3.5f, 6.8f, 4, 5.2f, .014f, 6.2f, 110, 260)},
    [NB_AVATAR_EXPR_CURIOUS] = {
        .name = "CURIOUS", .family = FAMILY_CURIOSITY, .springy = true,
        .peak = SHAPE(.v = 0.16f, .dh = 0.1f, .fy = -4.0f, .zoom = 0.02f, .pitch = -4.0f,
                      .roll = -7.0f),
        .settle = 1.0f, .antic_open = 0.89f, TIMES(120, 40, 230, 850, 260, 520),
        .blink = BLINK_ATTENTIVE,
        MOTION(3.5f, 1.15f, -1, 2, .9f, 3, .8f, .006f, 1.25f, 160, 300)},
    [NB_AVATAR_EXPR_CONFUSED] = {
        .name = "CONFUSED", .family = FAMILY_CURIOSITY, .springy = true,
        .peak = SHAPE(.v = 0.15f, .dw = -0.05f, .ly = 4.0f, .fy = 2.0f, .pitch = 3.5f,
                      .lid = 0.5f),
        .settle = 1.0f, .ant_amount = 1.0f, .roll_sway = 5.0f, TIMES(165, 300, 280, 420, 260, 720),
        .blink = BLINK_QUICK, MOTION(2, 1.1f, 0, 6, 4.2f, 3.5f, 3.8f, .004f, 1, 170, 320)},
    [NB_AVATAR_EXPR_ANGRY] = {
        .name = "ANGRY", .family = FAMILY_CONTRARY, .springy = true,
        .peak = SHAPE(.a = 1.0f, .spread = -11.0f, .ly = -9.0f, .fy = 12.0f, .zoom = 0.015f),
        .settle = 0.91f, .ant_amount = 0.73f, TIMES(145, 80, 230, 1160, 190, 350),
        .blink = BLINK_QUICK, MOTION(2, 3, 1.5f, 9, 32, 3.6f, 32, .004f, 2, 150, 220)},
    [NB_AVATAR_EXPR_SURPRISED] = {
        .name = "SURPRISED", .family = FAMILY_PRESENCE, .springy = true,
        .peak = SHAPE(.a = 1.0f, .spread = 5.0f, .fy = -4.0f, .zoom = 0.06f),
        .settle = 0.95f, .antic_open = 0.26f, TIMES(105, 0, 215, 810, 180, 430),
        .blink = BLINK_NONE, MOTION(3, 2, -1, 1.5f, 2.2f, 1.5f, 1.4f, .021f, 9, 120, 260)},
    [NB_AVATAR_EXPR_SAD] = {
        .name = "SAD", .family = FAMILY_SADNESS,
        .peak = SHAPE(.a = 0.22f, .dw = -0.15f, .dh = -0.25f, .spread = -9.0f, .ly = 35.0f,
                      .fy = 28.0f, .zoom = -0.015f, .pitch = 15.0f),
        .settle = 1.04f, .ant_amount = 0.46f, TIMES(280, 260, 430, 1250, 360, 760),
        .blink = BLINK_SLOW, MOTION(3.5f, .72f, 8, 1.2f, .55f, 1.2f, .48f, .0025f, .62f, 300, 460)},
    [NB_AVATAR_EXPR_SLEEPY] = {
        .name = "SLEEPY", .family = FAMILY_SLEEPINESS,
        .peak = SHAPE(.a = 0.4f, .spread = -10.0f, .zoom = -0.01f), .settle = 2.5f,
        TIMES(0, 0, 320, 380, 520, 1380), .blink = BLINK_NONE,
        MOTION(4, .9f, 9, 0, 0, 0, 0, .003f, .7f, 320, 420)},
    [NB_AVATAR_EXPR_DIZZY] = {
        .name = "DIZZY", .family = FAMILY_PRESENCE, .springy = true,
        .peak = SHAPE(.a = 1.0f, .v = 0.05f, .spread = -18.0f,
                      .fy = 3.0f, .zoom = 0.02f),
        .settle = 1.0f, .ant_amount = 1.0f, .roll_sway = -4.5f, .variant_sway = 0.05f,
        TIMES(180, 150, 210, 150, 210, 360), .blink = BLINK_NONE,
        MOTION(7, 6.4f, 0, 18, 10.5f, 5, 7, .012f, 4.2f, 140, 300)},
    [NB_AVATAR_EXPR_LOVING] = {
        .name = "LOVING", .family = FAMILY_JOY, .springy = true,
        .peak = SHAPE(.dw = 0.35f, .dh = -0.45f, .spread = -3.0f, .ly = 9.0f, .zoom = 0.03f,
                      .heart = 1.0f),
        .settle = 1.0f, .antic_open = 0.78f, TIMES(140, 0, 260, 700, 220, 600),
        .blink = BLINK_SLOW, MOTION(6, 2.4f, 0, 0, 0, 2, .9f, .005f, 1.6f, 220, 300)},
    [NB_AVATAR_EXPR_EMBARRASSED] = {
        .name = "EMBARRASSED", .family = FAMILY_SADNESS,
        .peak = SHAPE(.v = 1.0f, .dw = -0.2f, .dh = -0.3f, .lx = -22.0f, .ly = 45.0f,
                      .fy = 18.0f, .zoom = -0.01f, .pitch = 10.0f, .yaw = -6.0f),
        .settle = 0.93f, .antic_open = 0.92f, TIMES(140, 0, 300, 700, 400, 900),
        .blink = BLINK_ATTENTIVE, MOTION(2, .9f, 0, 1.5f, .8f, 2.5f, .7f, .003f, .9f, 220, 320)},
    [NB_AVATAR_EXPR_CONTENT] = {
        .name = "CONTENT", .family = FAMILY_JOY, .persistent = false,
        .peak = SHAPE(.dh = -0.08f, .spread = -3.0f, .ly = 6.0f, .fy = 1.0f),
        .settle = 1.0f, TIMES(0, 0, 320, 600, 400, 800), .blink = BLINK_CALM,
        MOTION(2.5f, .9f, 0, 0, 0, 0, 0, .003f, .8f, 300, 400)},
};

static const nb_avatar_recipe_t *recipe_of(nb_avatar_expression_t expression)
{
    return &s_recipes[expression];
}

static bool has_anticipation(const nb_avatar_recipe_t *recipe)
{
    return recipe->ant_ms > 0u;
}

static uint8_t recipe_frame_count(const nb_avatar_recipe_t *recipe)
{
    const bool settles = recipe->settle_ms > 0u || recipe->settle_hold_ms > 0u;
    return (uint8_t)((has_anticipation(recipe) ? 1 : 0) + 1 + (settles ? 1 : 0));
}

typedef struct {
    nb_avatar_pose_t pose;
    uint16_t transition_ms;
    uint16_t hold_ms;
    nb_avatar_easing_t easing;
} nb_avatar_keyframe_t;

/* The shape with its intensity-like scalars (and where the head looks) scaled by `ratio`. */
static nb_avatar_shape_t scaled(const nb_avatar_shape_t *peak, float ratio)
{
    nb_avatar_shape_t s = *peak;
    s.a *= ratio;
    s.lx *= ratio;
    s.ly *= ratio;
    s.pitch *= ratio;
    s.yaw *= ratio;
    return s;
}

/* The anticipation of a bouncy entrance: plain capsules, narrower and squashed. */
static nb_avatar_pose_t squash(const nb_avatar_pose_t *peak, float open)
{
    nb_avatar_pose_t pose = *peak;
    const float width = SQUASH_WIDTH * 0.5f * (peak->left_eye.width + peak->right_eye.width);
    const float height = REST_H * open;
    const float y = REST_Y + SQUASH_DROP * (REST_H - height);
    nb_avatar_eye_pose_t *eyes[2] = {&pose.left_eye, &pose.right_eye};
    for (int i = 0; i < 2; ++i) {
        *eyes[i] = make_eye(eyes[i]->x, y, width, height, 0, 0, 0);
    }
    pose.face_scale = 0.97f;
    pose.face_y = FACE_Y + 12.0f;
    pose.head_pitch = 0.0f;
    pose.head_yaw = 0.0f;
    pose.head_roll = 0.0f;
    return pose;
}

static void recipe_frame(const nb_avatar_recipe_t *recipe, uint8_t index,
                         nb_avatar_keyframe_t *out)
{
    const bool anticipates = has_anticipation(recipe);
    const nb_avatar_easing_t entry =
        recipe->springy ? NB_AVATAR_EASING_SPRING : NB_AVATAR_EASING_SMOOTH;
    nb_avatar_shape_t shape = recipe->peak;

    if (anticipates && index == 0u) {
        shape = scaled(&recipe->peak, recipe->ant_amount);
        shape.roll += recipe->roll_sway;
        shape.v += recipe->variant_sway;
        out->pose = generate(recipe->family, &shape);
        if (recipe->antic_open > 0.0f) {
            const nb_avatar_pose_t full = generate(recipe->family, &recipe->peak);
            out->pose = squash(&full, recipe->antic_open);
        }
        out->transition_ms = recipe->ant_ms;
        out->hold_ms = recipe->ant_hold_ms;
        out->easing = recipe->springy ? NB_AVATAR_EASING_SNAPPY : NB_AVATAR_EASING_SMOOTH;
        return;
    }
    if (index == (anticipates ? 1u : 0u)) {
        shape.roll -= recipe->roll_sway;
        shape.v -= recipe->variant_sway;
        out->pose = generate(recipe->family, &shape);
        out->transition_ms = recipe->peak_ms;
        out->hold_ms = recipe->peak_hold_ms;
        out->easing = entry;
        return;
    }
    shape = scaled(&recipe->peak, recipe->settle);
    /* A bouncy entrance calms down once settled; the others just keep the ratio. */
    shape.fy *= recipe->springy ? SETTLE_LIFT : recipe->settle;
    shape.zoom *= recipe->springy ? SETTLE_ZOOM : recipe->settle;
    shape.roll += SWAY_SETTLE * recipe->roll_sway;
    shape.v += SWAY_SETTLE * recipe->variant_sway;
    out->pose = generate(recipe->family, &shape);
    out->transition_ms = recipe->settle_ms;
    out->hold_ms = recipe->settle_hold_ms;
    out->easing = NB_AVATAR_EASING_SMOOTH;
}

/* The slow wander of an expression: face, eyes and scale. */
typedef struct {
    float face_x, face_y, eye_x, eye_y, scale;
} nb_avatar_motion_t;

static float wave(float amplitude, float angular_frequency, float phase, float time)
{
    if (amplitude == 0.0f || angular_frequency == 0.0f) {
        return 0.0f;
    }
    return sinf(time * angular_frequency + phase) * amplitude;
}

static nb_avatar_motion_t recipe_motion(const nb_avatar_recipe_t *recipe, float time)
{
    nb_avatar_motion_t m;
    m.face_x = wave(recipe->shake_amp, recipe->shake_w, 0.0f, time);
    m.face_y = recipe->bob_offset + wave(recipe->bob_amp, recipe->bob_w, 0.0f, time);
    m.eye_x = wave(recipe->drift_amp, recipe->drift_w, 0.0f, time);
    m.eye_y = wave(0.65f * recipe->drift_amp, 0.85f * recipe->drift_w, NB_AVATAR_PI * 0.5f, time);
    m.scale = 1.0f + wave(recipe->pulse_amp, recipe->pulse_w, 0.0f, time);
    return m;
}

static float clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static float easing(nb_avatar_easing_t kind, float progress)
{
    const float t = clamp01(progress);
    if (kind == NB_AVATAR_EASING_SNAPPY) {
        const float inverse = 1.0f - t;
        return 1.0f - inverse * inverse * inverse;
    }
    if (kind == NB_AVATAR_EASING_SPRING) {
        return t >= 1.0f ? 1.0f : 1.0f - expf(-6.5f * t) * cosf(10.0f * t);
    }
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float mixf(float from, float to, float amount)
{
    return from + (to - from) * amount;
}

static float attention_hash(uint32_t value, float seed)
{
    const float raw = sinf((float)value * 127.1f + seed * 311.7f) * 43758.5453f;
    return (raw - floorf(raw)) * 2.0f - 1.0f;
}

static float micro_saccade(uint32_t now_ms, float seed)
{
    const uint32_t step = now_ms / 1100u;
    const uint32_t elapsed = now_ms % 1100u;
    const float previous = step == 0u ? 0.0f : attention_hash(step - 1u, seed);
    const float next = attention_hash(step, seed);
    const float t = easing(NB_AVATAR_EASING_SMOOTH,
                           fminf(1.0f, (float)elapsed / 145.0f));
    return mixf(previous, next, t);
}

/* The eyes sit on the front of a spherical head that turns and is seen in
 * perspective: turning moves them across the face, the eye on the far side
 * gets smaller and narrower, the near one larger, and looking up or down
 * squashes them and tilts them toward each other as the face's vertical
 * lines converge. HEAD_TURN maps the head angle to the sphere's rotation;
 * below 1 it keeps the eyes on the screen at the widest looks. */
#define HEAD_RADIUS 520.0f
#define CAMERA_DISTANCE 1300.0f
#define HEAD_TURN 0.85f
#define TANGENT_STEP 10.0f

typedef struct {
    float x, y, z;
} vec3_t;

typedef struct {
    float cos_yaw, sin_yaw, cos_pitch, sin_pitch;
} head_turn_t;

// Point of the head's surface under a face point (front hemisphere).
static vec3_t on_head(float x, float y)
{
    const float r2 = HEAD_RADIUS * HEAD_RADIUS;
    vec3_t p = {x, y, sqrtf(fmaxf(r2 - x * x - y * y, r2 * 0.05f))};
    return p;
}

// Pitch about the horizontal axis (positive looks down), then yaw about the
// vertical one (positive looks right). Screen y grows downward, z toward the
// viewer.
static vec3_t turn(vec3_t p, const head_turn_t *t)
{
    const vec3_t a = {p.x, p.y * t->cos_pitch + p.z * t->sin_pitch,
                      -p.y * t->sin_pitch + p.z * t->cos_pitch};
    const vec3_t b = {a.x * t->cos_yaw + a.z * t->sin_yaw, a.y,
                      -a.x * t->sin_yaw + a.z * t->cos_yaw};
    return b;
}

// Perspective, normalized so a point at depth z_rest keeps its size.
static void to_screen(vec3_t p, float z_rest, float *x, float *y)
{
    const float s = (CAMERA_DISTANCE - z_rest) / (CAMERA_DISTANCE - p.z);
    *x = p.x * s;
    *y = p.y * s;
}

static void project_eye(nb_avatar_eye_pose_t *eye, float side, float head_yaw,
                        float head_pitch, float head_roll)
{
    (void)side;
    const float yaw = fmaxf(-50.0f, fminf(50.0f, head_yaw)) * HEAD_TURN * NB_AVATAR_PI / 180.0f;
    const float pitch =
        fmaxf(-45.0f, fminf(45.0f, head_pitch)) * HEAD_TURN * NB_AVATAR_PI / 180.0f;
    const head_turn_t rest = {1.0f, 0.0f, 1.0f, 0.0f};
    const head_turn_t now = {cosf(yaw), sinf(yaw), cosf(pitch), sinf(pitch)};

    const vec3_t center = on_head(eye->x, eye->y);
    const vec3_t across = on_head(eye->x + TANGENT_STEP, eye->y);
    const vec3_t down = on_head(eye->x, eye->y + TANGENT_STEP);
    float cx, cy, ax, ay, dx, dy;
    float rcx, rcy, rax, ray, rdx, rdy;
    to_screen(turn(center, &now), center.z, &cx, &cy);
    to_screen(turn(across, &now), center.z, &ax, &ay);
    to_screen(turn(down, &now), center.z, &dx, &dy);
    to_screen(turn(center, &rest), center.z, &rcx, &rcy);
    to_screen(turn(across, &rest), center.z, &rax, &ray);
    to_screen(turn(down, &rest), center.z, &rdx, &rdy);

    // How the eye's own horizontal and vertical stretch, against rest.
    const float width_scale = hypotf(ax - cx, ay - cy) / hypotf(rax - rcx, ray - rcy);
    const float height_scale = hypotf(dx - cx, dy - cy) / hypotf(rdx - rcx, rdy - rcy);
    const float tilt = atan2f(dx - cx, dy - cy) - atan2f(rdx - rcx, rdy - rcy);
    eye->x = cx;
    eye->y = cy;
    eye->width *= width_scale;
    eye->height *= height_scale;
    eye->angle -= tilt * 180.0f / NB_AVATAR_PI;

    const float roll = head_roll * NB_AVATAR_PI / 180.0f;
    const float rotated_x = eye->x * cosf(roll) - eye->y * sinf(roll);
    const float rotated_y = eye->x * sinf(roll) + eye->y * cosf(roll);
    eye->x = rotated_x;
    eye->y = rotated_y;
    eye->angle += head_roll;
}

static nb_avatar_eye_pose_t mix_eye(const nb_avatar_eye_pose_t *a,
                                    const nb_avatar_eye_pose_t *b, float t)
{
    nb_avatar_eye_pose_t out;
#define MIX_EYE(field) out.field = mixf(a->field, b->field, t)
    MIX_EYE(x);
    MIX_EYE(y);
    MIX_EYE(width);
    MIX_EYE(height);
    MIX_EYE(roundness);
    MIX_EYE(upper_lid);
    MIX_EYE(upper_lid_tilt);
    MIX_EYE(lower_lid);
    MIX_EYE(brow_y);
    MIX_EYE(brow_tilt);
    MIX_EYE(brow_opacity);
    MIX_EYE(angle);
    MIX_EYE(crescent);
    MIX_EYE(heart);
#undef MIX_EYE
    return out;
}

static nb_avatar_pose_t mix_pose(const nb_avatar_pose_t *a, const nb_avatar_pose_t *b, float t)
{
    nb_avatar_pose_t out;
    out.left_eye = mix_eye(&a->left_eye, &b->left_eye, t);
    out.right_eye = mix_eye(&a->right_eye, &b->right_eye, t);
    out.face_scale = mixf(a->face_scale, b->face_scale, t);
    out.face_x = mixf(a->face_x, b->face_x, t);
    out.face_y = mixf(a->face_y, b->face_y, t);
    out.head_yaw = mixf(a->head_yaw, b->head_yaw, t);
    out.head_pitch = mixf(a->head_pitch, b->head_pitch, t);
    out.head_roll = mixf(a->head_roll, b->head_roll, t);
    return out;
}

static uint32_t random_next(nb_avatar_core_state_t *state)
{
    state->rng_state = state->rng_state * 1664525u + 1013904223u;
    return state->rng_state;
}

static uint32_t blink_close_ms(const nb_avatar_core_state_t *state)
{
    return (uint32_t)((float)s_blink[recipe_of(state->expression)->blink].close_ms * state->blink_pace);
}

static uint32_t blink_open_ms(const nb_avatar_core_state_t *state)
{
    return (uint32_t)((float)s_blink[recipe_of(state->expression)->blink].open_ms * state->blink_pace);
}

static void schedule_blink(nb_avatar_core_state_t *state, bool initial)
{
    const nb_avatar_blink_t *blink = &s_blink[recipe_of(state->expression)->blink];
    state->blink_started_ms = 0u;
    if (!blink->enabled) {
        state->next_blink_ms = 0u;
        return;
    }
    uint32_t delay = blink->initial_delay_ms;
    if (!initial) {
        const uint32_t span = (uint32_t)blink->max_interval_ms - blink->min_interval_ms + 1u;
        delay = (uint32_t)((float)(blink->min_interval_ms + random_next(state) % span) *
                           state->blink_pace);
    }
    state->next_blink_ms = state->now_ms + delay;
}

static uint32_t expression_duration_ms(nb_avatar_expression_t expression)
{
    const nb_avatar_recipe_t *recipe = recipe_of(expression);
    return (uint32_t)recipe->ant_ms + recipe->ant_hold_ms + recipe->peak_ms +
           recipe->peak_hold_ms + recipe->settle_ms + recipe->settle_hold_ms;
}

void nb_avatar_core_init(nb_avatar_core_state_t *state, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->active = true;
    state->expression = NB_AVATAR_EXPR_IDLE;
    state->base_expression = NB_AVATAR_EXPR_IDLE;
    state->playback = NB_AVATAR_PLAY_LOOP;
    state->playback_direction = 1;
    state->rng_state = seed == 0u ? 0x4e423200u : seed;
    state->blink_override_scale = 1.0f;
    state->blink_pace = 1.0f;
    nb_avatar_keyframe_t rest;
    recipe_frame(recipe_of(NB_AVATAR_EXPR_IDLE), 0u, &rest);
    state->pose = rest.pose;
    state->from_pose = state->pose;
    schedule_blink(state, true);
}

bool nb_avatar_core_request(nb_avatar_core_state_t *state, nb_avatar_expression_t expression)
{
    if (state == NULL || expression >= NB_AVATAR_EXPR_COUNT) {
        return false;
    }
    const nb_avatar_recipe_t *recipe = recipe_of(expression);
    if (recipe->persistent) {
        state->base_expression = expression;
    }
    return nb_avatar_core_play(state, expression,
                               recipe->persistent ? NB_AVATAR_PLAY_LOOP : NB_AVATAR_PLAY_ONCE,
                               !recipe->persistent);
}

bool nb_avatar_core_play(nb_avatar_core_state_t *state, nb_avatar_expression_t expression,
                         nb_avatar_playback_t playback, bool return_to_base)
{
    if (state == NULL || expression >= NB_AVATAR_EXPR_COUNT ||
        playback > NB_AVATAR_PLAY_PINGPONG) {
        return false;
    }
    if (state->active && state->expression == expression && state->playback == playback &&
        state->return_to_base == return_to_base) {
        return true;
    }
    state->from_pose = state->pose;
    state->active = true;
    state->expression = expression;
    state->playback = return_to_base ? NB_AVATAR_PLAY_ONCE : playback;
    state->playback_direction = 1;
    state->return_to_base = return_to_base;
    state->holding = false;
    state->keyframe_index = 0u;
    state->frame_started_ms = state->now_ms;
    state->expression_started_ms = state->now_ms;
    state->expression_ends_ms = state->playback == NB_AVATAR_PLAY_ONCE
                                    ? state->now_ms + expression_duration_ms(expression)
                                    : 0u;
    schedule_blink(state, true);
    return true;
}

bool nb_avatar_core_hold(nb_avatar_core_state_t *state, nb_avatar_expression_t expression)
{
    if (state == NULL || expression >= NB_AVATAR_EXPR_COUNT) {
        return false;
    }
    if (recipe_of(expression)->persistent) {
        return nb_avatar_core_request(state, expression);
    }
    if (!(state->active && state->expression == expression &&
          state->playback == NB_AVATAR_PLAY_ONCE) &&
        !nb_avatar_core_play(state, expression, NB_AVATAR_PLAY_ONCE, false)) {
        return false;
    }
    state->return_to_base = false;
    state->holding = true;
    return true;
}

bool nb_avatar_core_release(nb_avatar_core_state_t *state, nb_avatar_expression_t base)
{
    if (state == NULL || base >= NB_AVATAR_EXPR_COUNT || !recipe_of(base)->persistent) {
        return false;
    }
    const bool first_pass = state->active && state->playback == NB_AVATAR_PLAY_ONCE &&
                            (state->holding || state->return_to_base) &&
                            state->now_ms < state->expression_ends_ms;
    if (first_pass) {
        state->base_expression = base;
        state->holding = false;
        state->return_to_base = true;
        return true;
    }
    return nb_avatar_core_request(state, base);
}

void nb_avatar_core_set_gaze_target(nb_avatar_core_state_t *state, float normalized_x,
                                    float normalized_y, bool enabled)
{
    if (state == NULL) {
        return;
    }
    state->gaze_enabled = enabled;
    state->gaze_target_x = enabled ? fmaxf(-1.0f, fminf(1.0f, normalized_x)) : 0.0f;
    state->gaze_target_y = enabled ? fmaxf(-1.0f, fminf(1.0f, normalized_y)) : 0.0f;
}

void nb_avatar_core_set_look(nb_avatar_core_state_t *state, const nb_avatar_look_t *look,
                             bool active)
{
    if (state == NULL) {
        return;
    }
    if (look != NULL) {
        state->look = *look;
    }
    state->look_target = active ? 1.0f : 0.0f;
}

void nb_avatar_core_set_eyes_closed(nb_avatar_core_state_t *state, bool closed)
{
    if (state == NULL) {
        return;
    }
    state->eyes_closed_target = closed ? 1.0f : 0.0f;
}

void nb_avatar_core_set_blink_pace(nb_avatar_core_state_t *state, float pace)
{
    if (state == NULL) {
        return;
    }
    state->blink_pace = fmaxf(0.5f, fminf(2.0f, pace));
}

void nb_avatar_core_set_blink_override(nb_avatar_core_state_t *state, float scale,
                                       bool enabled)
{
    if (state == NULL) {
        return;
    }
    state->blink_override_enabled = enabled;
    state->blink_override_scale = enabled ? clamp01(scale) : 1.0f;
}

static void update_gaze_spring(nb_avatar_core_state_t *state, uint32_t dt_ms)
{
    if (dt_ms == 0u) {
        return;
    }
    const float dt = fmaxf(0.001f, fminf(0.033f, (float)dt_ms / 1000.0f));
    const float target_x = state->gaze_target_x * 70.0f;
    const float target_y = state->gaze_target_y * 56.0f;
    const float stiffness = 185.0f;
    const float damping = 25.5f;
    state->gaze_velocity_x +=
        ((target_x - state->gaze_x) * stiffness - state->gaze_velocity_x * damping) * dt;
    state->gaze_velocity_y +=
        ((target_y - state->gaze_y) * stiffness - state->gaze_velocity_y * damping) * dt;
    state->gaze_x += state->gaze_velocity_x * dt;
    state->gaze_y += state->gaze_velocity_y * dt;

    const float target_yaw = state->gaze_target_x * 21.0f;
    const float target_pitch = state->gaze_target_y * 16.0f;
    const float target_roll = state->gaze_target_x * -4.0f;
    const float head_stiffness = 15.0f;
    const float head_damping = 7.0f;
#define UPDATE_HEAD(value_, velocity_, target_)                                                  \
    do {                                                                                         \
        (velocity_) += (((target_) - (value_)) * head_stiffness -                               \
                        (velocity_) * head_damping) * dt;                                        \
        (value_) += (velocity_) * dt;                                                            \
    } while (0)
    UPDATE_HEAD(state->head_yaw, state->head_yaw_velocity, target_yaw);
    UPDATE_HEAD(state->head_pitch, state->head_pitch_velocity, target_pitch);
    UPDATE_HEAD(state->head_roll, state->head_roll_velocity, target_roll);
#undef UPDATE_HEAD
}

void nb_avatar_core_tick(nb_avatar_core_state_t *state, uint32_t dt_ms)
{
    if (state == NULL || !state->active) {
        return;
    }
    state->now_ms += dt_ms;
    update_gaze_spring(state, dt_ms);
    if (state->eyes_closed < state->eyes_closed_target) {
        state->eyes_closed = fminf(state->eyes_closed_target,
                                   state->eyes_closed + (float)dt_ms / (float)NB_AVATAR_CLOSE_MS);
    } else {
        state->eyes_closed = fmaxf(state->eyes_closed_target,
                                   state->eyes_closed - (float)dt_ms / (float)NB_AVATAR_OPEN_MS);
    }
    const float fade = (float)dt_ms / (float)NB_AVATAR_LOOK_FADE_MS;
    state->look_weight = state->look_weight < state->look_target
                             ? fminf(state->look_target, state->look_weight + fade)
                             : fmaxf(state->look_target, state->look_weight - fade);
    const nb_avatar_blink_t *blink = &s_blink[recipe_of(state->expression)->blink];
    if (blink->enabled && state->blink_started_ms != 0u) {
        const uint32_t blink_duration = blink_close_ms(state) + blink_open_ms(state);
        if (state->now_ms - state->blink_started_ms >= blink_duration) {
            schedule_blink(state, false);
        }
    }
    if (blink->enabled && state->blink_started_ms == 0u && state->next_blink_ms != 0u &&
        state->now_ms >= state->next_blink_ms) {
        state->blink_started_ms = state->now_ms;
        state->next_blink_ms = 0u;
    }

    for (uint32_t advance = 0u; advance < NB_AVATAR_MAX_ADVANCES; ++advance) {
        const nb_avatar_recipe_t *recipe = recipe_of(state->expression);
        const uint8_t frame_count = recipe_frame_count(recipe);
        nb_avatar_keyframe_t frame_data;
        recipe_frame(recipe, state->keyframe_index, &frame_data);
        const nb_avatar_keyframe_t *frame = &frame_data;
        const uint32_t elapsed = state->now_ms - state->frame_started_ms;
        if (elapsed < frame->transition_ms) {
            const float t = frame->transition_ms == 0u
                                ? 1.0f
                                : (float)elapsed / (float)frame->transition_ms;
            state->pose = mix_pose(&state->from_pose, &frame->pose, easing(frame->easing, t));
            return;
        }
        state->pose = frame->pose;
        const uint32_t duration = (uint32_t)frame->transition_ms + frame->hold_ms;
        if (elapsed < duration) {
            return;
        }
        state->frame_started_ms += duration;
        state->from_pose = frame->pose;
        const int next_index = (int)state->keyframe_index + state->playback_direction;
        if (next_index >= 0 && next_index < frame_count) {
            state->keyframe_index = (uint8_t)next_index;
            continue;
        }
        if (state->playback == NB_AVATAR_PLAY_LOOP) {
            state->playback_direction = 1;
            state->keyframe_index = 0u;
            continue;
        }
        if (state->playback == NB_AVATAR_PLAY_PINGPONG && frame_count > 1u) {
            state->playback_direction = (int8_t)-state->playback_direction;
            state->keyframe_index =
                (uint8_t)((int)state->keyframe_index + state->playback_direction);
            continue;
        }
        if (state->return_to_base) {
            (void)nb_avatar_core_request(state, state->base_expression);
        }
        return;
    }
}

static float blink_scale(const nb_avatar_core_state_t *state)
{
    const nb_avatar_blink_t *blink = &s_blink[recipe_of(state->expression)->blink];
    if (!blink->enabled || state->blink_started_ms == 0u) {
        return 1.0f;
    }
    const uint32_t elapsed = state->now_ms - state->blink_started_ms;
    const uint32_t close_ms = blink_close_ms(state);
    const uint32_t open_ms = blink_open_ms(state);
    if (close_ms + open_ms == 0u || elapsed >= close_ms + open_ms) {
        return 1.0f;
    }
    const float closed = 0.08f;
    if (elapsed < close_ms) {
        return mixf(1.0f, closed, easing(NB_AVATAR_EASING_SMOOTH, (float)elapsed / close_ms));
    }
    return mixf(closed, 1.0f,
                easing(NB_AVATAR_EASING_SMOOTH, (float)(elapsed - close_ms) / open_ms));
}

void nb_avatar_core_get_frame(const nb_avatar_core_state_t *state,
                              nb_avatar_render_frame_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (state == NULL || !state->active) {
        return;
    }
    out->enabled = true;
    out->expression = state->expression;
    out->pose = state->pose;
    out->blink_scale = blink_scale(state);
    if (state->blink_override_enabled) {
        out->blink_scale = fminf(out->blink_scale, state->blink_override_scale);
    }
    if (state->eyes_closed > 0.0f) {
        // Closed eyes do not blink; the lid comes down as a curved cut.
        const float shut = easing(NB_AVATAR_EASING_SMOOTH, state->eyes_closed);
        out->blink_scale = mixf(out->blink_scale, 1.0f, shut);
        out->pose.left_eye.crescent = mixf(out->pose.left_eye.crescent, NB_AVATAR_SHUT, shut);
        out->pose.right_eye.crescent = mixf(out->pose.right_eye.crescent, NB_AVATAR_SHUT, shut);
        out->pose.left_eye.upper_lid *= 1.0f - shut;
        out->pose.right_eye.upper_lid *= 1.0f - shut;
        out->pose.left_eye.lower_lid *= 1.0f - shut;
        out->pose.right_eye.lower_lid *= 1.0f - shut;
    }
    out->animation_ms = state->now_ms;
    out->dizzy_overlay = state->expression == NB_AVATAR_EXPR_DIZZY;

    const nb_avatar_recipe_t *recipe = recipe_of(state->expression);
    const float time = (float)state->now_ms / 1000.0f;
    const nb_avatar_motion_t idle_motion = recipe_motion(recipe_of(NB_AVATAR_EXPR_IDLE), time);
    const nb_avatar_motion_t motion = recipe_motion(recipe, time);
    float emotion_blend = 0.0f;
    if (state->expression != NB_AVATAR_EXPR_IDLE) {
        emotion_blend = easing(
            NB_AVATAR_EASING_SMOOTH,
            (float)(state->now_ms - state->expression_started_ms) /
                (float)(recipe->blend_in_ms == 0u ? 1u : recipe->blend_in_ms));
        if (state->return_to_base && state->expression_ends_ms != 0u) {
            const uint32_t remaining = state->now_ms < state->expression_ends_ms
                                           ? state->expression_ends_ms - state->now_ms
                                           : 0u;
            emotion_blend *= easing(
                NB_AVATAR_EASING_SMOOTH,
                (float)remaining /
                    (float)(recipe->blend_out_ms == 0u ? 1u : recipe->blend_out_ms));
        }
        if (state->holding && state->now_ms > state->expression_ends_ms) {
            const float settle = easing(
                NB_AVATAR_EASING_SMOOTH,
                (float)(state->now_ms - state->expression_ends_ms) /
                    (float)(recipe->blend_out_ms == 0u ? 1u : recipe->blend_out_ms));
            emotion_blend *= mixf(1.0f, NB_AVATAR_HOLD_MOTION, settle);
        }
    }
#define MIX_MOTION(field_) mixf(idle_motion.field_, motion.field_, emotion_blend)
    const float face_x = MIX_MOTION(face_x);
    const float face_y = MIX_MOTION(face_y);
    const float eye_x = MIX_MOTION(eye_x);
    const float eye_y = MIX_MOTION(eye_y);
    const float scale = MIX_MOTION(scale);
#undef MIX_MOTION
    out->pose.face_x += face_x;
    out->pose.face_y += face_y;
    out->pose.face_scale *= scale;
    out->pose.left_eye.x += eye_x;
    out->pose.left_eye.y += eye_y;
    out->pose.right_eye.x += eye_x;
    out->pose.right_eye.y += eye_y;

    float attention_weight = state->look_weight;
    if (state->gaze_enabled) {
        const float magnitude = fminf(
            1.0f, sqrtf(state->gaze_target_x * state->gaze_target_x +
                        state->gaze_target_y * state->gaze_target_y));
        /* A weak external gaze is a subtle variation; only a strong explicit
         * direction suppresses the 2.5D choreography. */
        attention_weight *= 1.0f - clamp01((magnitude - 0.45f) / 0.35f) * 0.90f;
    }
    if (attention_weight > 0.0f) {
        nb_avatar_look_t attention = state->look;
        attention.eye_x += micro_saccade(state->now_ms, 17.29f) * 3.8f;
        attention.eye_y += micro_saccade(state->now_ms, 31.73f) * 2.3f;
        out->pose.left_eye.x += attention.eye_x * attention_weight;
        out->pose.left_eye.y += attention.eye_y * attention_weight;
        out->pose.right_eye.x += attention.eye_x * attention_weight;
        out->pose.right_eye.y += attention.eye_y * attention_weight;
        out->pose.head_yaw += attention.yaw * attention_weight;
        out->pose.head_pitch += attention.pitch * attention_weight;
        out->pose.head_roll += attention.roll * attention_weight;
        /* Pitch moves the whole face closer or farther: the vertical depth
         * cue that keeps the capsules close to the reference shapes. */
        const float pitch_depth = fmaxf(-1.0f, fminf(1.0f, attention.pitch / 28.0f));
        out->pose.face_scale *= 1.0f + pitch_depth * 0.055f * attention_weight;
    }
    out->pose.left_eye.x += state->gaze_x;
    out->pose.left_eye.y += state->gaze_y;
    out->pose.right_eye.x += state->gaze_x;
    out->pose.right_eye.y += state->gaze_y;
    out->pose.head_yaw += state->head_yaw;
    out->pose.head_pitch += state->head_pitch;
    out->pose.head_roll += state->head_roll;
    project_eye(&out->pose.left_eye, -1.0f, out->pose.head_yaw, out->pose.head_pitch,
                out->pose.head_roll);
    project_eye(&out->pose.right_eye, 1.0f, out->pose.head_yaw, out->pose.head_pitch,
                out->pose.head_roll);
}

const char *nb_avatar_core_expression_name(nb_avatar_expression_t expression)
{
    return expression < NB_AVATAR_EXPR_COUNT ? recipe_of(expression)->name : "INVALID";
}

bool nb_avatar_core_is_once_active(const nb_avatar_core_state_t *state)
{
    return state != NULL && state->active && state->playback == NB_AVATAR_PLAY_ONCE;
}
