#ifndef NB_ARC_H
#define NB_ARC_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_avatar.h"
#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// Arcs (docs/VIDA.md §3.4): reactions with a beginning, a middle and an end
// instead of a face that just shows up and goes. One arc at a time, each a
// fixed list of steps (expression, where to look, a slow blink or not):
//
// - Sulk and forgive: three quick taps in a row make the robot cross, turn
//   away with a slow blink, then look back and smile.
// - Make up: a negative mood and petting make it look at the person, still
//   sad, and then melt into love (or contentment if the petting stopped).
// - Search: long boredom at rest makes it look around for someone and sigh.
//
// Conversation, sleep and fault abort any arc at once, with no ending and no
// push on the mood. Time only comes from the caller and chance only from
// the seed.

#define NB_ARC_TAPS 3
#define NB_ARC_TAP_WINDOW_MS 10000u
#define NB_ARC_SULK_COOLDOWN_MS 60000u
#define NB_ARC_MAKE_UP_COOLDOWN_MS 30000u
#define NB_ARC_MAKE_UP_VALENCE (-0.15f)  // this far below rest, petting makes up
#define NB_ARC_SEARCH_BOREDOM 0.9f
#define NB_ARC_SEARCH_GAP_MS 3600000u    // at most one search an hour
#define NB_ARC_SEARCH_GLANCE_MS 1000u
#define NB_ARC_LOOK_MOVE_MS 350u         // eyes travel between steps this fast
#define NB_ARC_SLOW_BLINK_MS 650u
#define NB_ARC_MAX_STEPS 8

typedef enum {
    NB_ARC_NONE = 0,
    NB_ARC_SULK,
    NB_ARC_MAKE_UP,
    NB_ARC_SEARCH,
} nb_arc_kind_t;

typedef enum {
    NB_ARC_SEARCH_LATERAL = 0,
    NB_ARC_SEARCH_DIAGONAL,
    NB_ARC_SEARCH_AROUND,
    NB_ARC_SEARCH_PATTERNS,
} nb_arc_pattern_t;

typedef struct {
    nb_expression_t expression;
    nb_expression_t alone;  // shown instead once the petting stops; COUNT = none
    float look_x, look_y;   // direction -1..1, screen coordinates (y down)
    bool slow_blink;       // a slow blink at the start of the step
    bool aborts_without_petting;
    uint32_t duration_ms;
    float push_valence, push_arousal;  // mood push at the start of the step
} nb_arc_step_t;

// What the robot does around the arc, sampled by the caller.
typedef struct {
    bool resting;   // behavioral IDLE: a search may start and goes on
    bool busy;      // conversation, sleep or fault: every arc stops
    bool petting;   // a hold is going on
    bool drowsy;
    float valence;  // mood valence relative to its resting point
    float boredom;  // 0..1
} nb_arc_input_t;

typedef struct {
    nb_arc_kind_t kind;  // NONE: no arc, nothing to show
    nb_expression_t expression;
    nb_avatar_look_t look;
    float eye_open;      // 1 = open; lower during a slow blink
    uint32_t remaining_ms;
    // Set on the update that applies them, never again.
    nb_arc_kind_t started;
    nb_arc_kind_t ended;  // finished with its ending (not aborted)
    nb_arc_kind_t aborted;
    bool push;
    float push_valence, push_arousal;
} nb_arc_frame_t;

typedef struct {
    uint32_t rng;
    nb_arc_kind_t kind;
    nb_arc_step_t steps[NB_ARC_MAX_STEPS];
    int step_count;
    int step;
    uint32_t step_start_ms;
    bool step_entered;      // its push and blink were applied
    nb_avatar_look_t from;  // where the eyes were when the step began
    nb_avatar_look_t shown; // last look given out
    uint32_t taps[NB_ARC_TAPS];
    int tap_count;
    uint32_t sulk_free_ms, make_up_free_ms;  // not before these (valid if *_wait)
    bool sulk_wait, make_up_wait;
    uint32_t last_search_ms;
    bool searched;
    nb_arc_pattern_t last_pattern;
    nb_arc_kind_t pending_start;  // a tap that started a sulk, reported once
} nb_arc_t;

void nb_arc_init(nb_arc_t *arc, uint32_t seed);

// A quick tap. The third inside the window starts a sulk unless an arc is
// running or the last sulk was less than a minute ago.
void nb_arc_on_tap(nb_arc_t *arc, uint32_t now_ms);

// Advances to now_ms and fills out with what to show.
void nb_arc_update(nb_arc_t *arc, const nb_arc_input_t *in, uint32_t now_ms,
                   nb_arc_frame_t *out);

nb_arc_kind_t nb_arc_running(const nb_arc_t *arc);

// The search pattern of the running (or last) search.
nb_arc_pattern_t nb_arc_pattern(const nb_arc_t *arc);

const char *nb_arc_name(nb_arc_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif  // NB_ARC_H
