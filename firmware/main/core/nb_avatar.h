#ifndef NB_AVATAR_H
#define NB_AVATAR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The NB3 avatar: two solid eyes (capsules, crescents, slanted half-moons,
// circles) built from a capsule, lids and a crescent cut, first ported from
// the NoiseBot 2 avatar and redrawn for the face V2 (docs/FACE.md). Poses live in a 1000x1000 design space centered on the face;
// the renderer scales them to the screen. Time advances only through
// nb_avatar_core_tick(), randomness only through the seed.

typedef enum {
    NB_AVATAR_EXPR_IDLE = 0,
    NB_AVATAR_EXPR_LISTENING,
    NB_AVATAR_EXPR_THINKING,
    NB_AVATAR_EXPR_HAPPY,
    NB_AVATAR_EXPR_EXCITED,
    NB_AVATAR_EXPR_CURIOUS,
    NB_AVATAR_EXPR_CONFUSED,
    NB_AVATAR_EXPR_ANGRY,
    NB_AVATAR_EXPR_SURPRISED,
    NB_AVATAR_EXPR_SAD,
    NB_AVATAR_EXPR_SLEEPY,
    NB_AVATAR_EXPR_DIZZY,
    NB_AVATAR_EXPR_LOVING,
    NB_AVATAR_EXPR_EMBARRASSED,
    NB_AVATAR_EXPR_CONTENT,
    NB_AVATAR_EXPR_COUNT,
} nb_avatar_expression_t;

typedef struct {
    float x;               // eye center, design units, relative to the face
    float y;
    float width;
    float height;
    float roundness;       // 0 square .. 1 fully rounded
    float upper_lid;       // fraction of the eye covered from the top
    float upper_lid_tilt;  // slanted lid; sign picks the low side
    float lower_lid;       // fraction covered from the bottom
    float brow_y;          // brow offset from the eye center
    float brow_tilt;       // degrees
    float brow_opacity;    // 0 hidden .. 1 full
    float angle;           // eye rotation, degrees
    float crescent;        // >0 a curved cut rises from below (happy arc), <0 it
                           // comes down from above (sleepy arc); |1| leaves a sliver
} nb_avatar_eye_pose_t;

typedef struct {
    nb_avatar_eye_pose_t left_eye;
    nb_avatar_eye_pose_t right_eye;
    float face_scale;
    float face_x;
    float face_y;
    float head_yaw;
    float head_pitch;
    float head_roll;
} nb_avatar_pose_t;

// Where the face is looking: head turn (degrees) and eye slide inside the
// face (design units). Comes from outside (core/nb_gaze at rest).
typedef struct {
    float yaw;
    float pitch;
    float roll;
    float eye_x;
    float eye_y;
} nb_avatar_look_t;

typedef struct {
    bool enabled;
    bool dizzy_overlay;
    nb_avatar_expression_t expression;
    nb_avatar_pose_t pose;
    float blink_scale;     // eye height multiplier, 1 open .. ~0.08 closed
    uint32_t animation_ms;
} nb_avatar_render_frame_t;

typedef enum {
    NB_AVATAR_EASING_SMOOTH = 0,
    NB_AVATAR_EASING_SNAPPY,
    NB_AVATAR_EASING_SPRING,
} nb_avatar_easing_t;

typedef enum {
    NB_AVATAR_PLAY_ONCE = 0,
    NB_AVATAR_PLAY_LOOP,
    NB_AVATAR_PLAY_PINGPONG,
} nb_avatar_playback_t;

typedef struct {
    bool active;
    nb_avatar_expression_t expression;
    nb_avatar_expression_t base_expression;
    nb_avatar_playback_t playback;
    int8_t playback_direction;
    bool return_to_base;
    bool holding;  // a once expression kept on screen after its pass ends
    uint8_t keyframe_index;
    uint32_t now_ms;
    uint32_t frame_started_ms;
    uint32_t expression_started_ms;
    uint32_t expression_ends_ms;
    uint32_t next_blink_ms;
    uint32_t blink_started_ms;
    bool blink_override_enabled;
    float blink_override_scale;
    uint32_t rng_state;
    bool gaze_enabled;
    float gaze_target_x;
    float gaze_target_y;
    float gaze_x;
    float gaze_y;
    float gaze_velocity_x;
    float gaze_velocity_y;
    float head_yaw;
    float head_pitch;
    float head_roll;
    float head_yaw_velocity;
    float head_pitch_velocity;
    float head_roll_velocity;
    nb_avatar_look_t look;
    float look_weight;         // 0..1, eased toward look_target
    float look_target;
    float blink_pace;          // multiplies blink intervals and durations
    float eyes_closed;         // 0 open .. 1 shut, eased toward eyes_closed_target
    float eyes_closed_target;
    nb_avatar_pose_t from_pose;
    nb_avatar_pose_t pose;
} nb_avatar_core_state_t;

void nb_avatar_core_init(nb_avatar_core_state_t *state, uint32_t seed);

// Persistent expressions (IDLE, LISTENING, THINKING) become the base and
// loop; the others play once and return to the current base.
bool nb_avatar_core_request(nb_avatar_core_state_t *state, nb_avatar_expression_t expression);

bool nb_avatar_core_play(nb_avatar_core_state_t *state, nb_avatar_expression_t expression,
                         nb_avatar_playback_t playback, bool return_to_base);

// Shows an expression for as long as the caller wants it. Persistent ones
// behave as with request(); the others play once and then hold their final
// pose, with their motion settling to a calmer level, until something else
// is requested. Holding the expression already playing does not restart it.
bool nb_avatar_core_hold(nb_avatar_core_state_t *state, nb_avatar_expression_t expression);

// Ends a hold by returning to a persistent base. A once expression still in
// its first pass finishes it before returning, so a short reaction is never
// cut mid-gesture. Returns false if base is not persistent.
bool nb_avatar_core_release(nb_avatar_core_state_t *state, nb_avatar_expression_t base);

// Normalized gaze target in [-1, 1]; eyes and head follow with springs.
void nb_avatar_core_set_gaze_target(nb_avatar_core_state_t *state, float normalized_x,
                                    float normalized_y, bool enabled);

// The look to wander with, and whether it applies now. Turning it on or off
// fades over NB_AVATAR_LOOK_FADE_MS, so a reaction never jolts the eyes.
// The head turn, the 2.5D depth cue and the micro-saccades come with it.
#define NB_AVATAR_LOOK_FADE_MS 400u
void nb_avatar_core_set_look(nb_avatar_core_state_t *state, const nb_avatar_look_t *look,
                             bool active);

// Asleep: the eyes close into a thin lower arc (the top cut runs almost to
// the bottom) and stop blinking. They close slowly and open fast.
#define NB_AVATAR_CLOSE_MS 1200u
#define NB_AVATAR_OPEN_MS 300u
void nb_avatar_core_set_eyes_closed(nb_avatar_core_state_t *state, bool closed);

// Blink rhythm: >1 blinks less often and more slowly (0.5..2).
void nb_avatar_core_set_blink_pace(nb_avatar_core_state_t *state, float pace);

// External blink modulation (slow blinks); does not restart the timeline.
void nb_avatar_core_set_blink_override(nb_avatar_core_state_t *state, float scale,
                                       bool enabled);

void nb_avatar_core_tick(nb_avatar_core_state_t *state, uint32_t dt_ms);

void nb_avatar_core_get_frame(const nb_avatar_core_state_t *state,
                              nb_avatar_render_frame_t *out);

const char *nb_avatar_core_expression_name(nb_avatar_expression_t expression);

bool nb_avatar_core_is_once_active(const nb_avatar_core_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // NB_AVATAR_H
