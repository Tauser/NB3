#include "nb_avatar.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define NB_AVATAR_MAX_ADVANCES 64u
#define NB_AVATAR_PI 3.14159265358979323846f
/* Share of an expression's own motion kept while it is held: enough to stay
 * alive, calm enough that an ANGRY shake or HAPPY bounce can last a minute. */
#define NB_AVATAR_HOLD_MOTION 0.3f

typedef struct {
    float offset;
    float amplitude;
    float angular_frequency;
    float phase;
} nb_avatar_oscillator_t;

typedef struct {
    nb_avatar_oscillator_t face_x;
    nb_avatar_oscillator_t face_y;
    nb_avatar_oscillator_t eye_x;
    nb_avatar_oscillator_t eye_y;
    nb_avatar_oscillator_t scale;
    uint16_t blend_in_ms;
    uint16_t blend_out_ms;
} nb_avatar_motion_t;

typedef struct {
    bool enabled;
    uint16_t initial_delay_ms;
    uint16_t min_interval_ms;
    uint16_t max_interval_ms;
    uint16_t close_ms;
    uint16_t open_ms;
} nb_avatar_blink_t;

typedef struct {
    nb_avatar_pose_t pose;
    uint16_t transition_ms;
    uint16_t hold_ms;
    nb_avatar_easing_t easing;
} nb_avatar_keyframe_t;

typedef struct {
    const char *name;
    const nb_avatar_keyframe_t *frames;
    uint8_t frame_count;
    nb_avatar_playback_t playback;
    nb_avatar_blink_t blink;
    nb_avatar_motion_t motion;
    bool persistent;
} nb_avatar_spec_t;

typedef struct {
    float yaw;
    float pitch;
    float roll;
    float eye_x;
    float eye_y;
    uint16_t hold_ms;
    uint16_t transition_ms;
} nb_avatar_attention_t;

#define EYE(x_, y_, w_, h_, round_, upper_, upper_tilt_, lower_, brow_y_, brow_tilt_,         \
            brow_opacity_, angle_)                                                             \
    {                                                                                            \
        (x_), (y_), (w_), (h_), (round_), (upper_), (upper_tilt_), (lower_), (brow_y_),         \
            (brow_tilt_), (brow_opacity_), (angle_), 0.0f                                       \
    }
/* Face V2 pieces: a capsule, a capsule under a slanted upper lid (half-moon)
 * and a capsule with a crescent cut. No brows. */
#define CAP(x_, y_, w_, h_) EYE((x_), (y_), (w_), (h_), 1.0f, 0, 0, 0, -230.0f, 0, 0, 0)
#define LID(x_, y_, w_, h_, upper_, tilt_)                                                       \
    EYE((x_), (y_), (w_), (h_), 1.0f, (upper_), (tilt_), 0, -230.0f, 0, 0, 0)
#define CRES(x_, y_, w_, h_, crescent_)                                                          \
    {(x_), (y_), (w_), (h_), 1.0f, 0, 0, 0, -230.0f, 0, 0, 0, (crescent_)}
#define EYE_DEFAULT(x_, y_, w_, h_) EYE((x_), (y_), (w_), (h_), 1.0f, 0, 0, 0, -230.0f, 0, 0, 0)
#define EYE_ANGLED(x_, y_, w_, h_, angle_)                                                       \
    EYE((x_), (y_), (w_), (h_), 1.0f, 0, 0, 0, -230.0f, 0, 0, (angle_))
#define POSE(left_, right_, scale_, x_, y_, yaw_, pitch_, roll_)                                \
    {                                                                                            \
        left_, right_, (scale_), (x_), (y_), (yaw_), (pitch_), (roll_)                           \
    }
#define POSE_DEFAULT(left_, right_)                                                               \
    {left_, right_, 1.0f, 0, -8.0f, 0, 0, 0}
#define FRAME(pose_, transition_, hold_, easing_)                                                \
    {                                                                                            \
        pose_, (transition_), (hold_), (easing_)                                                 \
    }

static const nb_avatar_keyframe_t s_idle[] = {
    FRAME(POSE_DEFAULT(EYE_DEFAULT(-225, -15, 155, 360), EYE_DEFAULT(225, -15, 155, 360)), 260,
          0, NB_AVATAR_EASING_SMOOTH),
};

/* Face V2 (docs/FACE.md): a strong entrance, then a calm pose that is held
 * while the expression wins. The last keyframe of every one-shot expression
 * is that held pose. */

static const nb_avatar_keyframe_t s_listening[] = {
    /* Attentive: wider capsules, looking straight ahead, breathing slowly. */
    FRAME(POSE(CAP(-222, -18, 200, 325), CAP(222, -18, 200, 325), 1.0f, 0, -9, 0, -3, 0),
          300, 1200, NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(CAP(-222, -18, 190, 315), CAP(222, -18, 190, 315), 1.0f, 0, -9, 0, -2, 0),
          600, 1200, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_thinking[] = {
    /* Both eyes go up and to the side together, without tilting; the right
     * one a little smaller. */
    FRAME(POSE(CAP(-195, -60, 160, 333), CAP(250, -66, 160, 313), 1.0f, 0, -10, 6, -6, 0),
          420, 2200, NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(CAP(-200, -56, 160, 333), CAP(245, -62, 160, 300), 1.0f, 0, -10, 4, -5, 0),
          900, 2000, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_happy[] = {
    /* Capsules pop, then close into thick arcs. */
    FRAME(POSE(CAP(-225, 5, 180, 285), CAP(225, 5, 180, 285), .98f, 0, 4, 0, 0, 0), 115, 0,
          NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(CRES(-222, -10, 205, 195, .70f), CRES(222, -10, 205, 195, .70f), 1.035f, 0, -12,
               0, 0, 0),
          225, 900, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(CRES(-220, -14, 200, 185, .74f), CRES(220, -14, 200, 185, .74f), 1.015f, 0, -14,
               0, 0, 0),
          200, 420, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_excited[] = {
    /* Squash, jump into big capsules and stay big. */
    FRAME(POSE(CAP(-220, 18, 205, 112), CAP(220, 18, 205, 112), .965f, 0, 10, 0, 10, 0), 90, 25,
          NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(CAP(-225, -34, 225, 410), CAP(225, -34, 225, 410), 1.075f, 0, -24, 0, -11, 0), 155,
          180, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(CAP(-222, -22, 205, 395), CAP(222, -22, 205, 395), 1.035f, 0, -12, 0, -5, 0), 160,
          700, NB_AVATAR_EASING_SPRING),
};

static const nb_avatar_keyframe_t s_curious[] = {
    /* One eye grows, the other shrinks, the face tilts. */
    FRAME(POSE(CAP(-220, -16, 160, 320), CAP(220, -16, 160, 320), .99f, 0, -6, 0, 2, 0), 120, 40,
          NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(CAP(-220, -26, 190, 410), CAP(220, 0, 140, 280), 1.02f, 0, -12, 0, -4, -7), 230,
          850, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(CAP(-220, -24, 185, 400), CAP(220, -2, 138, 275), 1.01f, 0, -11, 0, -4, -6), 260,
          520, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_confused[] = {
    /* One half-moon, one capsule, swaying side to side. */
    FRAME(POSE(LID(-220, -10, 175, 300, .5f, 0), CAP(220, -20, 160, 340), .99f, 0, -5, 0, 4, 5),
          165, 300, NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(LID(-220, -10, 175, 300, .5f, 0), CAP(220, -20, 160, 340), 1.005f, 0, -8, 0, 3,
               -5),
          280, 420, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(LID(-220, -10, 175, 300, .5f, 0), CAP(220, -20, 160, 340), 1.0f, 0, -6, 0, 4, 3),
          260, 720, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_angry[] = {
    /* Half-moons slanted down toward the middle. */
    FRAME(POSE(LID(-218, -18, 200, 250, .10f, .55f), LID(218, -18, 200, 250, .10f, -.55f), .985f,
               0, 4, 0, 0, 0),
          145, 80, NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(LID(-214, -24, 205, 260, .14f, .75f), LID(214, -24, 205, 260, .14f, -.75f), 1.015f,
               0, 4, 0, 0, 0),
          230, 1160, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(LID(-218, -20, 200, 250, .12f, .68f), LID(218, -20, 200, 250, .12f, -.68f), 1.0f,
               0, 3, 0, 0, 0),
          190, 350, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_surprised[] = {
    FRAME(POSE(CAP(-225, 5, 205, 92), CAP(225, 5, 205, 92), .97f, 0, 2, 0, 0, 0), 105, 0,
          NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(CAP(-230, -15, 255, 255), CAP(230, -15, 255, 255), 1.06f, 0, -12, 0, 0, 0), 215,
          810, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(CAP(-228, -14, 240, 240), CAP(228, -14, 240, 240), 1.025f, 0, -10, 0, 0, 0), 180,
          430, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_sad[] = {
    /* Half-moons drooping to the outside, looking down. */
    FRAME(POSE(LID(-218, 4, 180, 270, .08f, -.30f), LID(218, 4, 180, 270, .08f, .30f), .995f, 0, 4,
               0, 5, 0),
          280, 260, NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(LID(-216, 20, 185, 255, .12f, -.65f), LID(216, 20, 185, 255, .12f, .65f), .985f, 0,
               20, 0, 15, 0),
          430, 1250, NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(LID(-216, 24, 185, 250, .12f, -.60f), LID(216, 24, 185, 250, .12f, .60f), .98f, 0,
               24, 0, 18, 0),
          360, 760, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_sleepy[] = {
    /* Capsules sink into upside-down arcs, like eyes closing. */
    FRAME(POSE(EYE(-215, 18, 245, 165, 1, .28f, 0, 0, -230, 0, 0, 0),
               EYE(215, 18, 245, 165, 1, .28f, 0, 0, -230, 0, 0, 0), .99f, 0, 18, 0, 0, 0),
          320, 380, NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(CRES(-215, 30, 220, 160, -.72f), CRES(215, 30, 220, 160, -.72f), .98f, 0, 26, 0, 0,
               0),
          520, 1380, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_dizzy[] = {
    FRAME(POSE(EYE_DEFAULT(-210, -10, 270, 270), EYE_DEFAULT(210, 4, 244, 244), 1.015f, 0, -5, 0,
               0, -4),
          180, 150, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(EYE_DEFAULT(-210, 5, 242, 242), EYE_DEFAULT(210, -12, 274, 274), 1.025f, 0, -2, 0,
               0, 5),
          210, 150, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(EYE_DEFAULT(-210, -7, 264, 264), EYE_DEFAULT(210, 2, 252, 252), 1.015f, 0, -5, 0,
               0, -3.5f),
          210, 360, NB_AVATAR_EASING_SPRING),
};

static const nb_avatar_keyframe_t s_loving[] = {
    /* Thin, flatter arcs than happy: the face of being petted. */
    FRAME(POSE(CAP(-225, 0, 190, 280), CAP(225, 0, 190, 280), .99f, 0, 2, 0, 0, 0), 140, 0,
          NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(CRES(-222, -6, 220, 170, .80f), CRES(222, -6, 220, 170, .80f), 1.03f, 0, -8, 0, 0,
               0),
          260, 700, NB_AVATAR_EASING_SPRING),
    FRAME(POSE(CRES(-222, -8, 215, 165, .82f), CRES(222, -8, 215, 165, .82f), 1.01f, 0, -8, 0, 0,
               0),
          220, 600, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_embarrassed[] = {
    /* Heavy half-moons looking down and away, then glancing back a little. */
    FRAME(POSE(CAP(-225, -10, 160, 330), CAP(225, -10, 160, 330), 1.0f, 0, -4, 0, 0, 0), 140, 0,
          NB_AVATAR_EASING_SNAPPY),
    FRAME(POSE(LID(-245, 40, 150, 250, .32f, -.30f), LID(195, 40, 150, 250, .32f, .30f), .99f, 0,
               10, -6, 10, 0),
          300, 700, NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(LID(-238, 36, 150, 250, .30f, -.28f), LID(202, 36, 150, 250, .30f, .28f), .99f, 0,
               8, -3, 8, 0),
          400, 900, NB_AVATAR_EASING_SMOOTH),
};

static const nb_avatar_keyframe_t s_content[] = {
    /* The mood at rest after warmth: lower, relaxed capsules. */
    FRAME(POSE(CAP(-222, -4, 170, 300), CAP(222, -4, 170, 300), 1.0f, 0, -6, 0, 0, 0), 320, 600,
          NB_AVATAR_EASING_SMOOTH),
    FRAME(POSE(CAP(-222, -2, 168, 285), CAP(222, -2, 168, 285), 1.0f, 0, -6, 0, 0, 0), 400, 800,
          NB_AVATAR_EASING_SMOOTH),
};

#define OSC(offset_, amp_, freq_, phase_) {(offset_), (amp_), (freq_), (phase_)}
#define MOTION(fx_, fy_, ex_, ey_, scale_, blend_in_, blend_out_)                              \
    {fx_, fy_, ex_, ey_, scale_, (blend_in_), (blend_out_)}
#define BLINK(enabled_, initial_, min_, max_, close_, open_)                                     \
    {(enabled_), (initial_), (min_), (max_), (close_), (open_)}
#define SPEC(name_, frames_, playback_, blink_, motion_, persistent_)                            \
    {(name_), (frames_), (uint8_t)(sizeof(frames_) / sizeof((frames_)[0])), (playback_),          \
     blink_, motion_, (persistent_)}

static const nb_avatar_spec_t s_specs[NB_AVATAR_EXPR_COUNT] = {
    SPEC("IDLE", s_idle, NB_AVATAR_PLAY_LOOP, BLINK(true, 2600, 3400, 6200, 112, 168),
         MOTION(OSC(0, 0, 0, 0), OSC(0, 3.2f, 1.15f, 0), OSC(0, 0, 0, 0), OSC(0, 0, 0, 0),
                OSC(1, .0045f, 1.05f, 0), 180, 240), true),
    SPEC("LISTENING", s_listening, NB_AVATAR_PLAY_LOOP, BLINK(true, 1900, 2800, 5200, 95, 145),
         MOTION(OSC(0, 1.2f, .65f, 0), OSC(-1, 2.5f, .85f, 0), OSC(0, 2.4f, .72f, 0),
                OSC(0, 1.5f, .58f, 0), OSC(1, .0035f, .95f, 0), 280, 360), true),
    SPEC("THINKING", s_thinking, NB_AVATAR_PLAY_LOOP, BLINK(true, 2600, 4200, 6800, 120, 180),
         MOTION(OSC(0, 2.8f, .52f, 0), OSC(-2, 3, .68f, 0), OSC(0, 4, .63f, 0),
                OSC(-2, 2.6f, .49f, NB_AVATAR_PI * .5f), OSC(1, .003f, .72f, 0), 320, 420),
         true),
    SPEC("HAPPY", s_happy, NB_AVATAR_PLAY_ONCE, BLINK(true, 1200, 1800, 3600, 72, 112),
         MOTION(OSC(0, 0, 0, 0), OSC(0, 15, 8, 0), OSC(0, 3.5f, 1.1f, 0),
                OSC(0, 2, .9f, NB_AVATAR_PI * .5f), OSC(1, .008f, 2.4f, 0), 180, 240), false),
    SPEC("EXCITED", s_excited, NB_AVATAR_PLAY_ONCE, BLINK(true, 1200, 1800, 3600, 72, 112),
         MOTION(OSC(0, 3.5f, 6.8f, 0), OSC(0, 15, 8.5f, 0), OSC(0, 4, 5.2f, 0),
                OSC(0, 3, 4.8f, 0), OSC(1, .014f, 6.2f, 0), 110, 260), false),
    SPEC("CURIOUS", s_curious, NB_AVATAR_PLAY_ONCE, BLINK(true, 1900, 2800, 5200, 95, 145),
         MOTION(OSC(0, 2, .9f, 0), OSC(-1, 3.5f, 1.15f, 0), OSC(0, 3, .8f, 0),
                OSC(0, 2, .7f, 0), OSC(1, .006f, 1.25f, 0), 160, 300), false),
    SPEC("CONFUSED", s_confused, NB_AVATAR_PLAY_ONCE, BLINK(true, 2100, 2800, 5000, 76, 116),
         MOTION(OSC(0, 6, 4.2f, 0), OSC(0, 2, 1.1f, 0), OSC(0, 3.5f, 3.8f, 0),
                OSC(0, 1.5f, 1.3f, 0), OSC(1, .004f, 1, 0), 170, 320), false),
    SPEC("ANGRY", s_angry, NB_AVATAR_PLAY_ONCE, BLINK(true, 2100, 2800, 5000, 76, 116),
         MOTION(OSC(0, 9, 32, 0), OSC(1.5f, 2, 3, 0), OSC(0, 3.6f, 32, 0),
                OSC(0, .8f, 2, 0), OSC(1, .004f, 2, 0), 150, 220), false),
    SPEC("SURPRISED", s_surprised, NB_AVATAR_PLAY_ONCE, BLINK(false, 0, 0, 0, 0, 0),
         MOTION(OSC(0, 1.5f, 2.2f, 0), OSC(-1, 3, 2, 0), OSC(0, 1.5f, 1.4f, 0),
                OSC(0, 1, 1.2f, NB_AVATAR_PI * .5f), OSC(1, .021f, 9, 0), 120, 260), false),
    SPEC("SAD", s_sad, NB_AVATAR_PLAY_ONCE, BLINK(true, 1800, 3200, 5600, 130, 190),
         MOTION(OSC(0, 1.2f, .55f, 0), OSC(8, 3.5f, .72f, 0), OSC(0, 1.2f, .48f, 0),
                OSC(2, 1, .5f, 0), OSC(1, .0025f, .62f, 0), 300, 460), false),
    SPEC("SLEEPY", s_sleepy, NB_AVATAR_PLAY_ONCE, BLINK(false, 0, 0, 0, 0, 0),
         MOTION(OSC(0, 0, 0, 0), OSC(9, 4, .9f, 0), OSC(0, 0, 0, 0), OSC(0, 0, 0, 0),
                OSC(1, .003f, .7f, 0), 320, 420), false),
    SPEC("DIZZY", s_dizzy, NB_AVATAR_PLAY_ONCE, BLINK(false, 0, 0, 0, 0, 0),
         MOTION(OSC(0, 18, 10.5f, 0), OSC(0, 7, 6.4f, 0), OSC(0, 5, 7, 0),
                OSC(0, 4, 8.8f, 0), OSC(1, .012f, 4.2f, 0), 140, 300), false),
    SPEC("LOVING", s_loving, NB_AVATAR_PLAY_ONCE, BLINK(true, 1800, 2800, 5200, 110, 170),
         MOTION(OSC(0, 0, 0, 0), OSC(0, 6, 2.4f, 0), OSC(0, 2, .9f, 0), OSC(0, 1.5f, .8f, 0),
                OSC(1, .005f, 1.6f, 0), 220, 300), false),
    SPEC("EMBARRASSED", s_embarrassed, NB_AVATAR_PLAY_ONCE,
         BLINK(true, 1800, 2800, 5200, 95, 145),
         MOTION(OSC(0, 1.5f, .8f, 0), OSC(0, 2, .9f, 0), OSC(0, 2.5f, .7f, 0),
                OSC(0, 1, .6f, 0), OSC(1, .003f, .9f, 0), 220, 320), false),
    SPEC("CONTENT", s_content, NB_AVATAR_PLAY_ONCE, BLINK(true, 2600, 3400, 6400, 140, 200),
         MOTION(OSC(0, 0, 0, 0), OSC(0, 2.5f, .9f, 0), OSC(0, 0, 0, 0), OSC(0, 0, 0, 0),
                OSC(1, .003f, .8f, 0), 300, 400), false),
};

static const nb_avatar_attention_t s_idle_attention[] = {
    /* Autonomous IDLE choreography. Vertical looks come early in the cycle
     * and hold long enough to be noticed; they change no state. */
    {0, 0, 0, 0, 0, 2600, 450},
    /* Pure vertical looks, which the reference avatar drives from an IMU;
     * here they are part of the choreography instead. */
    {0, -26, 0, 0, -82, 4500, 550},
    {0, 0, 0, 0, 0, 1800, 450},
    /* A downward fixation in the same vocabulary as the side/diagonal ones. */
    {0, 26, 0, 0, 82, 4500, 550},
    {24, -10, 4.8f, 12, -5, 5200, 560},
    {-20, 14, -5.5f, -10, 7, 3600, 520},
    {11, -16, 2.2f, 6, -8, 4500, 500},
    {-27, -4, -4, -12, 1, 5000, 640},
};

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

static nb_avatar_attention_t mix_attention(const nb_avatar_attention_t *a,
                                           const nb_avatar_attention_t *b, float t)
{
    nb_avatar_attention_t out = {
        .yaw = mixf(a->yaw, b->yaw, t),
        .pitch = mixf(a->pitch, b->pitch, t),
        .roll = mixf(a->roll, b->roll, t),
        .eye_x = mixf(a->eye_x, b->eye_x, t),
        .eye_y = mixf(a->eye_y, b->eye_y, t),
        .hold_ms = 0,
        .transition_ms = 0,
    };
    return out;
}

static nb_avatar_attention_t sample_idle_attention(uint32_t now_ms)
{
    const size_t count = sizeof(s_idle_attention) / sizeof(s_idle_attention[0]);
    uint32_t cycle_ms = 0u;
    for (size_t i = 0; i < count; ++i) {
        cycle_ms += s_idle_attention[i].hold_ms + s_idle_attention[i].transition_ms;
    }
    uint32_t cursor = cycle_ms == 0u ? 0u : now_ms % cycle_ms;
    for (size_t i = 0; i < count; ++i) {
        const nb_avatar_attention_t *pose = &s_idle_attention[i];
        if (cursor < pose->hold_ms) {
            return *pose;
        }
        cursor -= pose->hold_ms;
        if (cursor < pose->transition_ms) {
            const float t = easing(NB_AVATAR_EASING_SMOOTH,
                                   (float)cursor / (float)pose->transition_ms);
            return mix_attention(pose, &s_idle_attention[(i + 1u) % count], t);
        }
        cursor -= pose->transition_ms;
    }
    return s_idle_attention[0];
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

static void project_eye(nb_avatar_eye_pose_t *eye, float side, float head_yaw,
                        float head_pitch, float head_roll)
{
    const float yaw = fmaxf(-1.0f, fminf(1.0f, head_yaw / 35.0f));
    const float pitch = fmaxf(-1.0f, fminf(1.0f, head_pitch / 28.0f));
    const float horizontal_compression = 1.0f - fabsf(yaw) * 0.16f;
    /* Pitch changes the shape only slightly: the reference keeps vertical
     * capsules even at the extremes, so depth reads from offset and face
     * scale (applied later) rather than from flattening the eyes. */
    const float vertical_compression = 1.0f - fabsf(pitch) * 0.08f;
    const float vertical_widening = 1.0f + fabsf(pitch) * 0.04f;
    const float depth_scale =
        fmaxf(0.62f, 1.0f + side * yaw * 0.28f - fabsf(pitch) * 0.02f);
    eye->x = eye->x * horizontal_compression + yaw * 96.0f + side * pitch * 12.0f;
    eye->y += pitch * 62.0f;
    eye->width *= depth_scale * (1.0f - fabsf(yaw) * 0.12f) * vertical_widening;
    eye->height *= depth_scale * vertical_compression;
    const float roll = head_roll * NB_AVATAR_PI / 180.0f;
    const float rotated_x = eye->x * cosf(roll) - eye->y * sinf(roll);
    const float rotated_y = eye->x * sinf(roll) + eye->y * cosf(roll);
    eye->x = rotated_x;
    eye->y = rotated_y;
    eye->angle += head_roll + yaw * 7.0f + side * yaw * 3.5f + side * pitch * 3.5f;
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

static void schedule_blink(nb_avatar_core_state_t *state, bool initial)
{
    const nb_avatar_blink_t *blink = &s_specs[state->expression].blink;
    state->blink_started_ms = 0u;
    if (!blink->enabled) {
        state->next_blink_ms = 0u;
        return;
    }
    uint32_t delay = blink->initial_delay_ms;
    if (!initial) {
        const uint32_t span = (uint32_t)blink->max_interval_ms - blink->min_interval_ms + 1u;
        delay = blink->min_interval_ms + random_next(state) % span;
    }
    state->next_blink_ms = state->now_ms + delay;
}

static uint32_t expression_duration_ms(nb_avatar_expression_t expression)
{
    const nb_avatar_spec_t *spec = &s_specs[expression];
    uint32_t duration_ms = 0u;
    for (uint8_t i = 0u; i < spec->frame_count; ++i) {
        duration_ms += (uint32_t)spec->frames[i].transition_ms + spec->frames[i].hold_ms;
    }
    return duration_ms;
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
    state->pose = s_idle[0].pose;
    state->from_pose = state->pose;
    schedule_blink(state, true);
}

bool nb_avatar_core_request(nb_avatar_core_state_t *state, nb_avatar_expression_t expression)
{
    if (state == NULL || expression >= NB_AVATAR_EXPR_COUNT) {
        return false;
    }
    const nb_avatar_spec_t *spec = &s_specs[expression];
    if (spec->persistent) {
        state->base_expression = expression;
    }
    return nb_avatar_core_play(state, expression, spec->playback, !spec->persistent);
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
    if (s_specs[expression].persistent) {
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
    if (state == NULL || base >= NB_AVATAR_EXPR_COUNT || !s_specs[base].persistent) {
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
    const nb_avatar_blink_t *blink = &s_specs[state->expression].blink;
    if (blink->enabled && state->blink_started_ms != 0u) {
        const uint32_t blink_duration = (uint32_t)blink->close_ms + blink->open_ms;
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
        const nb_avatar_spec_t *spec = &s_specs[state->expression];
        const nb_avatar_keyframe_t *frame = &spec->frames[state->keyframe_index];
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
        if (next_index >= 0 && next_index < spec->frame_count) {
            state->keyframe_index = (uint8_t)next_index;
            continue;
        }
        if (state->playback == NB_AVATAR_PLAY_LOOP) {
            state->playback_direction = 1;
            state->keyframe_index = 0u;
            continue;
        }
        if (state->playback == NB_AVATAR_PLAY_PINGPONG && spec->frame_count > 1u) {
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

static float sample_oscillator(const nb_avatar_oscillator_t *oscillator, float time)
{
    if (oscillator->amplitude == 0.0f || oscillator->angular_frequency == 0.0f) {
        return oscillator->offset;
    }
    return oscillator->offset +
           sinf(time * oscillator->angular_frequency + oscillator->phase) *
               oscillator->amplitude;
}

static float blink_scale(const nb_avatar_core_state_t *state)
{
    const nb_avatar_blink_t *blink = &s_specs[state->expression].blink;
    if (!blink->enabled || state->blink_started_ms == 0u) {
        return 1.0f;
    }
    const uint32_t elapsed = state->now_ms - state->blink_started_ms;
    const uint32_t duration = (uint32_t)blink->close_ms + blink->open_ms;
    if (duration == 0u || elapsed >= duration) {
        return 1.0f;
    }
    const float closed = 0.08f;
    if (elapsed < blink->close_ms) {
        return mixf(1.0f, closed,
                    easing(NB_AVATAR_EASING_SMOOTH, (float)elapsed / blink->close_ms));
    }
    return mixf(closed, 1.0f,
                easing(NB_AVATAR_EASING_SMOOTH,
                       (float)(elapsed - blink->close_ms) / blink->open_ms));
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
    out->animation_ms = state->now_ms;
    out->dizzy_overlay = state->expression == NB_AVATAR_EXPR_DIZZY;

    const nb_avatar_motion_t *idle_motion = &s_specs[NB_AVATAR_EXPR_IDLE].motion;
    const nb_avatar_motion_t *motion = &s_specs[state->expression].motion;
    const float time = (float)state->now_ms / 1000.0f;
    float emotion_blend = 0.0f;
    if (state->expression != NB_AVATAR_EXPR_IDLE) {
        emotion_blend = easing(
            NB_AVATAR_EASING_SMOOTH,
            (float)(state->now_ms - state->expression_started_ms) /
                (float)(motion->blend_in_ms == 0u ? 1u : motion->blend_in_ms));
        if (state->return_to_base && state->expression_ends_ms != 0u) {
            const uint32_t remaining = state->now_ms < state->expression_ends_ms
                                           ? state->expression_ends_ms - state->now_ms
                                           : 0u;
            emotion_blend *= easing(
                NB_AVATAR_EASING_SMOOTH,
                (float)remaining /
                    (float)(motion->blend_out_ms == 0u ? 1u : motion->blend_out_ms));
        }
        if (state->holding && state->now_ms > state->expression_ends_ms) {
            const float settle = easing(
                NB_AVATAR_EASING_SMOOTH,
                (float)(state->now_ms - state->expression_ends_ms) /
                    (float)(motion->blend_out_ms == 0u ? 1u : motion->blend_out_ms));
            emotion_blend *= mixf(1.0f, NB_AVATAR_HOLD_MOTION, settle);
        }
    }
#define MIX_MOTION(field_)                                                                      \
    mixf(sample_oscillator(&idle_motion->field_, time), sample_oscillator(&motion->field_, time), \
         emotion_blend)
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

    const bool autonomous_attention = state->expression == NB_AVATAR_EXPR_IDLE;
    float attention_weight = 1.0f;
    if (state->gaze_enabled) {
        const float magnitude = fminf(
            1.0f, sqrtf(state->gaze_target_x * state->gaze_target_x +
                        state->gaze_target_y * state->gaze_target_y));
        /* A weak external gaze is a subtle variation; only a strong explicit
         * direction suppresses the 2.5D choreography. */
        attention_weight = 1.0f - clamp01((magnitude - 0.45f) / 0.35f) * 0.90f;
    }
    if (autonomous_attention) {
        nb_avatar_attention_t attention = sample_idle_attention(state->now_ms);
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
    return expression < NB_AVATAR_EXPR_COUNT ? s_specs[expression].name : "INVALID";
}

bool nb_avatar_core_is_once_active(const nb_avatar_core_state_t *state)
{
    return state != NULL && state->active && state->playback == NB_AVATAR_PLAY_ONCE;
}
