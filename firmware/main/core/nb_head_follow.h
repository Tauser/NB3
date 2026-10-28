#ifndef NB_HEAD_FOLLOW_H
#define NB_HEAD_FOLLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_head_gesture.h"

#ifdef __cplusplus
extern "C" {
#endif

// The head's own behavior (docs/ROADMAP.md stage 4): what it does when
// nobody asks it anything. Decides moves, never sends them; the motion task
// plays them through motion_safety like any other.
//
// - Follows the face: when the eyes look somewhere on their own (looking
//   around at rest, an arc), the head turns a fraction of the way with
//   them. Only a change worth it moves it, and not too often, so the
//   servos do not hum along with every flick of the eyes.
// - A tap gets a small nod; while petted the head leans down into the hand.
// - Asleep, the head droops a little.
// - The AI comes first: after one of its moves the head does nothing on its
//   own for a while.
//
// Face angles follow the screen (yaw > 0 = looking to the robot's left,
// pitch > 0 = looking down); the output is in the head's angles (pitch > 0 =
// up). Time only comes from the caller.

typedef struct {
    float yaw_gain, pitch_gain;  // head degrees per face degree
    float min_change_deg;        // smaller changes are not worth a move
    uint32_t min_gap_ms;         // between two follow moves
    uint32_t follow_ms;          // a follow move takes at least this long
    uint32_t ai_hold_ms;         // quiet after a move asked by the AI
    uint32_t tap_gap_ms;         // between two tap nods
    float pet_pitch_deg;         // leaning into the hand
    float sleep_pitch_deg;       // drooping while asleep
} nb_head_follow_cfg_t;

typedef enum {
    NB_FOLLOW_AWAKE = 0,
    NB_FOLLOW_TALKING,  // conversation: the face looks at the user, so does the head
    NB_FOLLOW_ASLEEP,
} nb_follow_mode_t;

typedef struct {
    bool can_move;      // the safety would allow it (not stopped, no servo out of range)
    nb_follow_mode_t mode;
    bool face_own;      // the eyes look on their own (else they look at the user)
    float face_yaw, face_pitch;  // degrees, screen convention
    bool petting;
} nb_head_follow_input_t;

typedef struct {
    bool move;
    nb_head_move_t kind;   // LOOK or NOD
    float yaw, pitch;      // LOOK, head degrees
    int repeats;           // NOD
    uint32_t min_time_ms;  // a LOOK lasts at least this long
} nb_head_follow_out_t;

typedef struct {
    nb_head_follow_cfg_t cfg;
    bool started;          // a target was sent at least once
    float last_yaw, last_pitch;
    uint32_t last_move_ms;
    uint32_t ai_until_ms;
    bool ai_hold;
    bool tap_pending;
    bool tap_seen;
    uint32_t last_tap_ms;
} nb_head_follow_t;

nb_head_follow_cfg_t nb_head_follow_default_cfg(void);
void nb_head_follow_init(nb_head_follow_t *f, const nb_head_follow_cfg_t *cfg);

// The AI moved the head (or relaxed it): stay out of its way for a while.
void nb_head_follow_on_ai(nb_head_follow_t *f, uint32_t now_ms);
void nb_head_follow_on_tap(nb_head_follow_t *f, uint32_t now_ms);

void nb_head_follow_update(nb_head_follow_t *f, const nb_head_follow_input_t *in,
                           uint32_t now_ms, nb_head_follow_out_t *out);

#ifdef __cplusplus
}
#endif

#endif  // NB_HEAD_FOLLOW_H
