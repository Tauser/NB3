#include "nb_arc.h"

#include <math.h>
#include <string.h>

// The reach of a full look, the same as the gaze at rest (core/nb_gaze), so
// an arc looks as far as the robot does when it looks around.
#define YAW_MAX 27.0f
#define PITCH_MAX 26.0f
#define ROLL_MAX 5.0f
#define EYE_X_MAX 8.0f
#define EYE_Y_MAX 20.0f

#define NB_ARC_PI 3.14159265358979323846f

static uint32_t next_u32(nb_arc_t *arc) {
    arc->rng = arc->rng * 1664525u + 1013904223u;
    return arc->rng;
}

static float next_unit(nb_arc_t *arc) {
    return (float)(next_u32(arc) >> 8) / 16777216.0f;
}

static bool reached(uint32_t now_ms, uint32_t at_ms) {
    return (int32_t)(now_ms - at_ms) >= 0;
}

static nb_avatar_look_t look_at(float x, float y) {
    nb_avatar_look_t l = {x * YAW_MAX, y * PITCH_MAX, x * ROLL_MAX, x * EYE_X_MAX, y * EYE_Y_MAX};
    return l;
}

static float mixf(float a, float b, float t) { return a + (b - a) * t; }

static nb_avatar_look_t mix_look(const nb_avatar_look_t *a, const nb_avatar_look_t *b, float t) {
    nb_avatar_look_t l = {mixf(a->yaw, b->yaw, t), mixf(a->pitch, b->pitch, t),
                          mixf(a->roll, b->roll, t), mixf(a->eye_x, b->eye_x, t),
                          mixf(a->eye_y, b->eye_y, t)};
    return l;
}

static nb_arc_step_t step(nb_expression_t expression, float x, float y, uint32_t duration_ms) {
    nb_arc_step_t s;
    memset(&s, 0, sizeof(s));
    s.expression = expression;
    s.alone = NB_EXPR_COUNT;
    s.look_x = x;
    s.look_y = y;
    s.duration_ms = duration_ms;
    return s;
}

void nb_arc_init(nb_arc_t *arc, uint32_t seed) {
    memset(arc, 0, sizeof(*arc));
    arc->rng = seed ^ 0x2545F491u;
    arc->last_pattern = NB_ARC_SEARCH_PATTERNS;
}

static void begin(nb_arc_t *arc, nb_arc_kind_t kind, uint32_t now_ms) {
    arc->kind = kind;
    arc->step = 0;
    arc->step_start_ms = now_ms;
    arc->step_entered = false;
    memset(&arc->from, 0, sizeof(arc->from));
    arc->shown = arc->from;
    arc->pending_start = kind;
}

static void build_sulk(nb_arc_t *arc) {
    const float side = next_unit(arc) < 0.5f ? -1.0f : 1.0f;
    nb_arc_step_t *s = arc->steps;
    s[0] = step(NB_EXPR_ANGRY, 0.0f, 0.25f, 1500);  // cross, eyes a little down
    // Hurt rather than angry: once the sulk's own face is over, the mood
    // shows sad, which asks for comfort.
    s[0].valence_to = -0.35f;
    s[0].push_arousal = -0.1f;
    s[1] = step(NB_EXPR_ANGRY, side * 0.8f, 0.2f, 1500);  // turns away
    s[1].slow_blink = true;
    // Looks back and smiles: forgiven, though still a little hurt until
    // someone makes up with it.
    s[2] = step(NB_EXPR_HAPPY, 0.0f, 0.0f, 1000);
    arc->step_count = 3;
}

static void build_make_up(nb_arc_t *arc) {
    nb_arc_step_t *s = arc->steps;
    s[0] = step(NB_EXPR_SAD, 0.0f, -0.3f, 400);  // looks at the person
    s[0].aborts_without_petting = true;
    // Still sad a moment. The hand may already be gone: petting starts
    // counting 1.5 s after the touch, so a 3 s caress leaves little more.
    s[1] = step(NB_EXPR_SAD, 0.0f, -0.3f, 800);
    s[2] = step(NB_EXPR_LOVING, 0.0f, -0.2f, 2500);  // made up
    s[2].alone = NB_EXPR_CONTENT;
    s[2].slow_blink = true;
    s[2].valence_to = 0.35f;  // happier than at rest: making up brings closer
    s[2].push_arousal = -0.1f;
    arc->step_count = 3;
}

static void build_search(nb_arc_t *arc) {
    // Each pattern is a few hops; a search takes the first 2 to 4.
    static const float kHops[NB_ARC_SEARCH_PATTERNS][4][2] = {
        [NB_ARC_SEARCH_LATERAL] = {{-0.85f, 0.0f}, {0.85f, 0.0f}, {-0.85f, 0.0f}, {0.85f, 0.0f}},
        [NB_ARC_SEARCH_DIAGONAL] = {{-0.75f, -0.5f}, {0.75f, 0.4f}, {0.75f, -0.5f}, {-0.75f, 0.4f}},
        [NB_ARC_SEARCH_AROUND] = {{0.85f, 0.0f}, {0.0f, -0.7f}, {-0.85f, 0.0f}, {0.0f, 0.6f}},
    };
    nb_arc_pattern_t pattern = (nb_arc_pattern_t)(next_u32(arc) % NB_ARC_SEARCH_PATTERNS);
    if (pattern == arc->last_pattern) {
        pattern = (nb_arc_pattern_t)((pattern + 1 + next_u32(arc) % 2) % NB_ARC_SEARCH_PATTERNS);
    }
    arc->last_pattern = pattern;
    const int glances = 2 + (int)(next_u32(arc) % 3);
    const float mirror = next_unit(arc) < 0.5f ? -1.0f : 1.0f;
    nb_arc_step_t *s = arc->steps;
    s[0] = step(NB_EXPR_CURIOUS, 0.0f, -0.25f, 500);  // perks up
    for (int i = 0; i < glances; i++) {
        s[1 + i] = step(NB_EXPR_CURIOUS, mirror * kHops[pattern][i][0], kHops[pattern][i][1],
                        NB_ARC_SEARCH_GLANCE_MS);
    }
    s[1 + glances] = step(NB_EXPR_SAD, 0.0f, 0.3f, 1800);  // nobody: a sigh
    s[1 + glances].slow_blink = true;
    arc->step_count = 2 + glances;
}

void nb_arc_on_tap(nb_arc_t *arc, uint32_t now_ms) {
    if (arc->kind != NB_ARC_NONE) {
        return;  // taps during an arc do not count toward the next sulk
    }
    if (arc->tap_count < NB_ARC_TAPS) {
        arc->taps[arc->tap_count++] = now_ms;
    } else {
        memmove(arc->taps, arc->taps + 1, sizeof(arc->taps[0]) * (NB_ARC_TAPS - 1));
        arc->taps[NB_ARC_TAPS - 1] = now_ms;
    }
    if (arc->tap_count < NB_ARC_TAPS || now_ms - arc->taps[0] > NB_ARC_TAP_WINDOW_MS) {
        return;
    }
    if (arc->sulk_wait && !reached(now_ms, arc->sulk_free_ms)) {
        return;
    }
    arc->tap_count = 0;
    build_sulk(arc);
    begin(arc, NB_ARC_SULK, now_ms);
}

static void stop(nb_arc_t *arc) {
    arc->kind = NB_ARC_NONE;
    arc->step_count = 0;
    arc->pending_start = NB_ARC_NONE;
}

static void finish(nb_arc_t *arc, uint32_t now_ms) {
    if (arc->kind == NB_ARC_SULK) {
        arc->sulk_free_ms = now_ms + NB_ARC_SULK_COOLDOWN_MS;
        arc->sulk_wait = true;
    } else if (arc->kind == NB_ARC_MAKE_UP) {
        arc->make_up_free_ms = now_ms + NB_ARC_MAKE_UP_COOLDOWN_MS;
        arc->make_up_wait = true;
    }
    stop(arc);
}

static void maybe_start(nb_arc_t *arc, const nb_arc_input_t *in, uint32_t now_ms) {
    if (in->busy) {
        return;
    }
    if (in->petting && in->valence < NB_ARC_MAKE_UP_VALENCE &&
        !(arc->make_up_wait && !reached(now_ms, arc->make_up_free_ms))) {
        build_make_up(arc);
        begin(arc, NB_ARC_MAKE_UP, now_ms);
        return;
    }
    if (in->resting && !in->drowsy && in->boredom >= NB_ARC_SEARCH_BOREDOM &&
        !(arc->searched && now_ms - arc->last_search_ms < NB_ARC_SEARCH_GAP_MS)) {
        arc->searched = true;
        arc->last_search_ms = now_ms;
        build_search(arc);
        begin(arc, NB_ARC_SEARCH, now_ms);
    }
}

static bool must_abort(const nb_arc_t *arc, const nb_arc_input_t *in) {
    if (in->busy || (arc->kind == NB_ARC_SEARCH && !in->resting)) {
        return true;
    }
    return arc->steps[arc->step].aborts_without_petting && !in->petting;
}

void nb_arc_update(nb_arc_t *arc, const nb_arc_input_t *in, uint32_t now_ms,
                   nb_arc_frame_t *out) {
    memset(out, 0, sizeof(*out));
    out->kind = NB_ARC_NONE;
    out->expression = NB_EXPR_NEUTRAL;
    out->eye_open = 1.0f;
    out->started = NB_ARC_NONE;
    out->ended = NB_ARC_NONE;
    out->aborted = NB_ARC_NONE;

    if (arc->kind == NB_ARC_NONE) {
        maybe_start(arc, in, now_ms);
    }
    if (arc->kind == NB_ARC_NONE) {
        return;
    }
    if (must_abort(arc, in)) {
        // A sulk started by a tap and aborted at once was never shown.
        out->aborted = arc->pending_start == NB_ARC_NONE ? arc->kind : NB_ARC_NONE;
        stop(arc);
        return;
    }
    out->started = arc->pending_start;
    arc->pending_start = NB_ARC_NONE;

    // Walk the steps the time has gone through, applying each push once.
    for (;;) {
        const nb_arc_step_t *s = &arc->steps[arc->step];
        if (!arc->step_entered) {
            arc->step_entered = true;
            float dv = s->valence_to - (in->valence + out->push_valence);
            if (s->valence_to < 0.0f) {
                dv = fminf(dv, 0.0f);
            } else if (s->valence_to > 0.0f) {
                dv = fmaxf(dv, 0.0f);
            } else {
                dv = 0.0f;
            }
            if (dv != 0.0f || s->push_arousal != 0.0f) {
                out->push = true;
                out->push_valence += dv;
                out->push_arousal += s->push_arousal;
            }
        }
        if (now_ms - arc->step_start_ms < s->duration_ms) {
            break;
        }
        arc->step_start_ms += s->duration_ms;
        arc->from = arc->shown;
        arc->step++;
        arc->step_entered = false;
        if (arc->step >= arc->step_count) {
            out->ended = arc->kind;
            finish(arc, now_ms);
            return;
        }
        if (must_abort(arc, in)) {
            out->aborted = arc->kind;
            out->push = false;
            stop(arc);
            return;
        }
    }

    const nb_arc_step_t *s = &arc->steps[arc->step];
    const uint32_t elapsed = now_ms - arc->step_start_ms;
    const nb_avatar_look_t target = look_at(s->look_x, s->look_y);
    float t = (float)elapsed / (float)NB_ARC_LOOK_MOVE_MS;
    t = t > 1.0f ? 1.0f : t;
    t = t * t * (3.0f - 2.0f * t);
    arc->shown = mix_look(&arc->from, &target, t);

    out->kind = arc->kind;
    out->expression = (s->alone != NB_EXPR_COUNT && !in->petting) ? s->alone : s->expression;
    out->look = arc->shown;
    if (s->slow_blink && elapsed < NB_ARC_SLOW_BLINK_MS) {
        out->eye_open = 1.0f - 0.9f * sinf(NB_ARC_PI * (float)elapsed / NB_ARC_SLOW_BLINK_MS);
    }
    uint32_t remaining = s->duration_ms - elapsed;
    for (int i = arc->step + 1; i < arc->step_count; i++) {
        remaining += arc->steps[i].duration_ms;
    }
    out->remaining_ms = remaining;
}

nb_arc_kind_t nb_arc_running(const nb_arc_t *arc) { return arc->kind; }

nb_arc_pattern_t nb_arc_pattern(const nb_arc_t *arc) { return arc->last_pattern; }

const char *nb_arc_name(nb_arc_kind_t kind) {
    switch (kind) {
        case NB_ARC_SULK:
            return "sulk";
        case NB_ARC_MAKE_UP:
            return "make up";
        case NB_ARC_SEARCH:
            return "search";
        default:
            return "none";
    }
}
