#include <math.h>
#include <string.h>

#include "nb_arc.h"
#include "nb_test.h"

#define STEP_MS 20u

static nb_arc_input_t calm(void) {
    nb_arc_input_t in;
    memset(&in, 0, sizeof(in));
    in.resting = true;
    return in;
}

typedef struct {
    nb_arc_kind_t started, ended, aborted;
    int pushes;
    float push_valence, push_arousal;
    float min_eye_open;
    float max_step_yaw;  // largest look change between two frames
    nb_expression_t seen[16];
    int seen_count;
    nb_arc_frame_t last;
} run_t;

// Runs the arc from *now for ms, accumulating what the frames said.
static void run(nb_arc_t *arc, const nb_arc_input_t *in, uint32_t *now, uint32_t ms, run_t *r) {
    memset(r, 0, sizeof(*r));
    r->min_eye_open = 1.0f;
    nb_arc_frame_t f;
    bool have_prev = false;
    float prev_yaw = 0.0f;
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        nb_arc_update(arc, in, *now, &f);
        *now += STEP_MS;
        if (f.started != NB_ARC_NONE) {
            r->started = f.started;
        }
        if (f.ended != NB_ARC_NONE) {
            r->ended = f.ended;
        }
        if (f.aborted != NB_ARC_NONE) {
            r->aborted = f.aborted;
        }
        if (f.push) {
            r->pushes++;
            r->push_valence += f.push_valence;
            r->push_arousal += f.push_arousal;
        }
        if (f.kind != NB_ARC_NONE) {
            r->min_eye_open = fminf(r->min_eye_open, f.eye_open);
            if (have_prev) {
                r->max_step_yaw = fmaxf(r->max_step_yaw, fabsf(f.look.yaw - prev_yaw));
            }
            prev_yaw = f.look.yaw;
            have_prev = true;
            if (r->seen_count == 0 || r->seen[r->seen_count - 1] != f.expression) {
                if (r->seen_count < 16) {
                    r->seen[r->seen_count++] = f.expression;
                }
            }
        } else {
            have_prev = false;
        }
        r->last = f;
    }
}

static void taps(nb_arc_t *arc, uint32_t *now, int count, uint32_t gap_ms) {
    for (int i = 0; i < count; i++) {
        nb_arc_on_tap(arc, *now);
        *now += gap_ms;
    }
}

static void test_three_quick_taps_sulk_then_forgive(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 7u);
    uint32_t now = 1000;
    const nb_arc_input_t in = calm();
    run_t r;
    taps(&arc, &now, 2, 1500);
    run(&arc, &in, &now, 200, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);  // two taps are just taps
    taps(&arc, &now, 1, 0);
    run(&arc, &in, &now, 1400, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.started);
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, r.last.expression);
    NB_CHECK(r.last.look.pitch > 0.0f);  // eyes a little down
    NB_CHECK_EQ_INT(1, r.pushes);
    NB_CHECK(r.push_valence < -0.5f && r.push_arousal > 0.1f);
    // Turns away with a slow blink, still cross.
    run(&arc, &in, &now, 1500, &r);
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, r.last.expression);
    NB_CHECK(fabsf(r.last.look.yaw) > 15.0f);
    NB_CHECK(r.min_eye_open < 0.3f);
    NB_CHECK(r.max_step_yaw < 6.0f);  // the head turns, it does not jump
    // Looks back and smiles: forgiven, the mood left as it was.
    run(&arc, &in, &now, 900, &r);
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, r.last.expression);
    NB_CHECK(fabsf(r.last.look.yaw) < 1.0f);
    NB_CHECK_EQ_INT(0, r.pushes);
    run(&arc, &in, &now, 300, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.ended);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.last.kind);
    NB_CHECK_EQ_INT(NB_ARC_NONE, nb_arc_running(&arc));
}

static void test_slow_taps_and_cooldown(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 3u);
    uint32_t now = 0;
    const nb_arc_input_t in = calm();
    run_t r;
    taps(&arc, &now, 3, 5100);  // three taps over 10.2 s
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    taps(&arc, &now, 3, 300);
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.started);
    taps(&arc, &now, 5, 100);  // ignored while sulking
    run(&arc, &in, &now, 5000, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.ended);
    taps(&arc, &now, 3, 300);  // within a minute of the forgiving
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    now += NB_ARC_SULK_COOLDOWN_MS;
    taps(&arc, &now, 3, 300);
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.started);
}

static void test_conversation_aborts_without_ending(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 5u);
    uint32_t now = 0;
    nb_arc_input_t in = calm();
    run_t r;
    taps(&arc, &now, 3, 200);
    run(&arc, &in, &now, 2000, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.started);
    in.busy = true;
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.aborted);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.ended);
    NB_CHECK_EQ_INT(0, r.pushes);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.last.kind);
    // Busy, nothing starts at all.
    in.petting = true;
    in.valence = -0.5f;
    taps(&arc, &now, 3, 200);
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.last.kind);
}

static void test_petting_a_sad_robot_makes_up(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 11u);
    uint32_t now = 0;
    nb_arc_input_t in = calm();
    run_t r;
    in.petting = true;
    run(&arc, &in, &now, 500, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);  // content robot: petting is petting
    in.valence = -0.3f;
    run(&arc, &in, &now, 1800, &r);
    NB_CHECK_EQ_INT(NB_ARC_MAKE_UP, r.started);
    NB_CHECK_EQ_INT(NB_EXPR_SAD, r.last.expression);
    NB_CHECK(r.last.look.pitch < 0.0f);  // looks up at the person
    NB_CHECK_EQ_INT(0, r.pushes);
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_EXPR_LOVING, r.last.expression);
    NB_CHECK_EQ_INT(1, r.pushes);
    NB_CHECK(r.push_valence >= 0.5f);
    NB_CHECK(r.min_eye_open < 0.3f);
    in.petting = false;  // the hand leaves: content, not aborted
    run(&arc, &in, &now, 500, &r);
    NB_CHECK_EQ_INT(NB_EXPR_CONTENT, r.last.expression);
    run(&arc, &in, &now, 1500, &r);
    NB_CHECK_EQ_INT(NB_ARC_MAKE_UP, r.ended);
    // Within 30 s another petting of a sad robot is just petting.
    in.petting = true;
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    now += NB_ARC_MAKE_UP_COOLDOWN_MS;
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_MAKE_UP, r.started);
}

static void test_make_up_aborts_if_the_hand_leaves_early(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 13u);
    uint32_t now = 0;
    nb_arc_input_t in = calm();
    run_t r;
    in.petting = true;
    in.valence = -0.4f;
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_MAKE_UP, r.started);
    in.petting = false;
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_MAKE_UP, r.aborted);
    NB_CHECK_EQ_INT(0, r.pushes);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.last.kind);
}

static void test_a_sulk_turns_into_making_up(void) {
    // Provoke, sulk, then pet: the sulk leaves the mood low enough that the
    // petting reconciles. The shell would feed the pushes into the mood.
    nb_arc_t arc;
    nb_arc_init(&arc, 17u);
    uint32_t now = 0;
    nb_arc_input_t in = calm();
    run_t r;
    taps(&arc, &now, 3, 300);
    run(&arc, &in, &now, 300, &r);
    in.valence += r.push_valence;
    in.petting = true;  // a hand comes during the sulk: one arc at a time
    run(&arc, &in, &now, 3000, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.last.kind);
    in.valence += r.push_valence;
    in.petting = false;
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_SULK, r.ended);
    in.petting = true;
    NB_CHECK(in.valence < NB_ARC_MAKE_UP_VALENCE);
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_MAKE_UP, r.started);
}

static void test_boredom_searches_at_most_hourly(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 19u);
    uint32_t now = 0;
    nb_arc_input_t in = calm();
    run_t r;
    in.boredom = 0.85f;
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    in.boredom = 0.95f;
    in.drowsy = true;  // a sleepy robot does not look for anyone
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    in.drowsy = false;
    in.resting = false;
    run(&arc, &in, &now, 1000, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    in.resting = true;
    run(&arc, &in, &now, 8000, &r);
    NB_CHECK_EQ_INT(NB_ARC_SEARCH, r.started);
    NB_CHECK_EQ_INT(NB_ARC_SEARCH, r.ended);
    NB_CHECK_EQ_INT(NB_EXPR_CURIOUS, r.seen[0]);
    NB_CHECK_EQ_INT(NB_EXPR_SAD, r.seen[r.seen_count - 1]);  // nobody: a sigh
    NB_CHECK(r.min_eye_open < 0.3f);
    NB_CHECK_EQ_INT(0, r.pushes);
    // Still bored, but not again within the hour.
    run(&arc, &in, &now, 60000, &r);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.started);
    now += NB_ARC_SEARCH_GAP_MS;
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_SEARCH, r.started);
}

static void test_someone_coming_ends_the_search(void) {
    nb_arc_t arc;
    nb_arc_init(&arc, 23u);
    uint32_t now = 0;
    nb_arc_input_t in = calm();
    run_t r;
    in.boredom = 1.0f;
    run(&arc, &in, &now, 1500, &r);
    NB_CHECK_EQ_INT(NB_ARC_SEARCH, r.started);
    in.resting = false;  // a touch or a conversation
    run(&arc, &in, &now, 100, &r);
    NB_CHECK_EQ_INT(NB_ARC_SEARCH, r.aborted);
    NB_CHECK_EQ_INT(NB_ARC_NONE, r.last.kind);
}

static void test_searches_vary(void) {
    // Over many seeds: 2 to 4 glances, every pattern shows up, and a search
    // never repeats the pattern of the one before.
    int glance_seen[5] = {0};
    int pattern_seen[NB_ARC_SEARCH_PATTERNS] = {0};
    for (uint32_t seed = 1; seed <= 40; seed++) {
        nb_arc_t arc;
        nb_arc_init(&arc, seed);
        uint32_t now = 0;
        nb_arc_input_t in = calm();
        in.boredom = 1.0f;
        nb_arc_pattern_t prev = NB_ARC_SEARCH_PATTERNS;
        for (int search = 0; search < 4; search++) {
            run_t r;
            run(&arc, &in, &now, 8000, &r);
            NB_CHECK_EQ_INT(NB_ARC_SEARCH, r.ended);
            const nb_arc_pattern_t p = nb_arc_pattern(&arc);
            NB_CHECK(p != prev);
            prev = p;
            pattern_seen[p]++;
            // CURIOUS for the perk and the glances, then the sigh.
            NB_CHECK_EQ_INT(2, r.seen_count);
            now += NB_ARC_SEARCH_GAP_MS;
        }
        // Glance count from the arc length: 500 + n * 1000 + 1800.
        nb_arc_frame_t f;
        nb_arc_update(&arc, &in, now, &f);
        const uint32_t glances = (f.remaining_ms + STEP_MS - 500u - 1800u) / NB_ARC_SEARCH_GLANCE_MS;
        NB_CHECK(glances >= 2 && glances <= 4);
        glance_seen[glances]++;
    }
    NB_CHECK(glance_seen[2] > 0 && glance_seen[3] > 0 && glance_seen[4] > 0);
    for (int p = 0; p < NB_ARC_SEARCH_PATTERNS; p++) {
        NB_CHECK(pattern_seen[p] > 0);
    }
}

static void test_names(void) {
    NB_CHECK(strcmp(nb_arc_name(NB_ARC_SULK), "sulk") == 0);
    NB_CHECK(strcmp(nb_arc_name(NB_ARC_NONE), "none") == 0);
}

int main(void) {
    NB_RUN(test_three_quick_taps_sulk_then_forgive);
    NB_RUN(test_slow_taps_and_cooldown);
    NB_RUN(test_conversation_aborts_without_ending);
    NB_RUN(test_petting_a_sad_robot_makes_up);
    NB_RUN(test_make_up_aborts_if_the_hand_leaves_early);
    NB_RUN(test_a_sulk_turns_into_making_up);
    NB_RUN(test_boredom_searches_at_most_hourly);
    NB_RUN(test_someone_coming_ends_the_search);
    NB_RUN(test_searches_vary);
    NB_RUN(test_names);
    NB_TEST_MAIN_END();
}
