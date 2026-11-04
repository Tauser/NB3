#include <math.h>
#include <string.h>

#include "nb_overlay.h"
#include "nb_test.h"

typedef struct {
    nb_overlay_t core;
    nb_overlay_input_t in;
} rig_t;

static void rig_init(rig_t *r) {
    nb_overlay_init(&r->core);
    memset(&r->in, 0, sizeof(r->in));
    r->in.winner = NB_EXPR_NEUTRAL;
}

static nb_overlay_frame_t step(rig_t *r, uint32_t now) {
    nb_overlay_frame_t f;
    nb_overlay_update(&r->core, &r->in, now, &f);
    return f;
}

static void test_rest_shows_nothing(void) {
    rig_t r;
    rig_init(&r);
    nb_overlay_frame_t f = step(&r, 0);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, f.activity);
    NB_CHECK_EQ_INT(NB_GESTURE_NONE, f.gesture);
    NB_CHECK_EQ_INT(NB_TECH_NONE, f.tech);
    NB_CHECK(!f.cheeks);
}

static void test_bars_follow_real_listening_not_the_expression(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    // Any expression, commanded during listening, leaves the bars alone.
    const nb_expression_t any[] = {NB_EXPR_NEUTRAL, NB_EXPR_HAPPY, NB_EXPR_SAD, NB_EXPR_ANGRY,
                                   NB_EXPR_SURPRISED, NB_EXPR_LISTENING};
    uint32_t t = 0;
    for (size_t i = 0; i < sizeof(any) / sizeof(any[0]); i++) {
        r.in.winner = any[i];
        NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, t += 33).activity);
    }
    // The listening expression without a real microphone invents no bars.
    r.in.listening = false;
    r.in.winner = NB_EXPR_LISTENING;
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, t += 33).activity);
}

static void test_bars_open_at_first_then_close_when_the_person_goes_quiet(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    // The microphone just opened: ears open until they start.
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 0).activity);
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 4000).activity);
    r.in.voice = true;
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 4100).activity);
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 6000).activity);
    // They stop: the bars hold a short tail and close, though listening goes on.
    r.in.voice = false;
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 6200).activity);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, 6600).activity);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, 20000).activity);
    // They speak again in the same spell: the bars come back.
    r.in.voice = true;
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 20100).activity);
}

static void test_bars_give_up_if_the_person_never_speaks(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 0).activity);
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, NB_OVERLAY_BARS_ATTENTION_MS - 1u).activity);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, NB_OVERLAY_BARS_ATTENTION_MS).activity);
}

static void test_a_new_listening_spell_opens_the_bars_again(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    r.in.voice = true;
    step(&r, 0);
    r.in.voice = false;
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, 1000).activity);
    r.in.listening = false;  // the reply is spoken and listening ends
    step(&r, 2000);
    r.in.listening = true;
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 30000).activity);
}

static void test_speaking_wins_over_listening_and_the_expression(void) {
    rig_t r;
    rig_init(&r);
    r.in.winner = NB_EXPR_LOVING;
    r.in.listening = true;
    r.in.speaking = true;
    r.in.audio_level = 0.5f;
    nb_overlay_frame_t f = step(&r, 0);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, f.activity);
    // Hearts are G: they do not give way to the bubble.
    NB_CHECK_EQ_INT(NB_GESTURE_HEARTS, f.gesture);
    NB_CHECK(f.cheeks);
}

static void test_bubble_pops_then_settles(void) {
    rig_t r;
    rig_init(&r);
    step(&r, 0);
    r.in.speaking = true;
    NB_CHECK(step(&r, 10).bubble < 0.1f);
    NB_CHECK(step(&r, 190).bubble > 1.05f);
    NB_CHECK(fabsf(step(&r, 600).bubble - 1.0f) < 1e-4f);
}

static void test_bubble_lingers_then_leaves(void) {
    rig_t r;
    rig_init(&r);
    r.in.speaking = true;
    step(&r, 0);
    step(&r, 1000);
    r.in.speaking = false;
    nb_overlay_frame_t f = step(&r, 1000);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, f.activity);
    f = step(&r, 1250);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, f.activity);
    NB_CHECK(f.bubble > 0.1f && f.bubble < 0.9f);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, 1500).activity);
    // Listening right after the reply takes over once the bubble is gone.
    r.in.listening = true;
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, step(&r, 1600).activity);
}

static void test_voice_back_during_linger_keeps_the_bubble(void) {
    rig_t r;
    rig_init(&r);
    r.in.speaking = true;
    step(&r, 0);
    r.in.speaking = false;
    step(&r, 1000);
    r.in.speaking = true;
    nb_overlay_frame_t f = step(&r, 1200);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, f.activity);
    NB_CHECK(fabsf(f.bubble - 1.0f) < 1e-4f);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, step(&r, 2500).activity);
}

static void test_thinking_hand_waits_then_slides_in(void) {
    rig_t r;
    rig_init(&r);
    r.in.winner = NB_EXPR_THINKING;
    step(&r, 0);
    NB_CHECK(step(&r, 900).hand == 0.0f);
    const float half = step(&r, 1125).hand;
    NB_CHECK(half > 0.2f && half < 0.8f);
    nb_overlay_frame_t f = step(&r, 2000);
    NB_CHECK_EQ_INT(NB_GESTURE_HAND, f.gesture);
    NB_CHECK(f.hand == 1.0f);
    NB_CHECK(f.hand_angle <= 0.0f && f.hand_angle >= -7.0f);
    // A new thinking spell starts the wait again.
    r.in.winner = NB_EXPR_NEUTRAL;
    step(&r, 2100);
    r.in.winner = NB_EXPR_THINKING;
    step(&r, 2200);
    NB_CHECK(step(&r, 2500).hand == 0.0f);
}

static void test_thinking_during_listening_keeps_the_bars_and_no_hand(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    r.in.winner = NB_EXPR_THINKING;
    for (uint32_t t = 0; t <= 3000; t += 100) {
        nb_overlay_frame_t f = step(&r, t);
        NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, f.activity);
        NB_CHECK_EQ_INT(NB_GESTURE_NONE, f.gesture);
    }
}

static void test_confirmed_wait_replaces_the_bars_with_the_hand_at_once(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    step(&r, 0);
    r.in.wait_confirmed = true;
    step(&r, 100);
    nb_overlay_frame_t f = step(&r, 450);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, f.activity);
    NB_CHECK_EQ_INT(NB_GESTURE_HAND, f.gesture);
    NB_CHECK(f.hand > 0.9f);  // no thinking delay: the producer already waited
    // The wait ends: the hand goes, the bars return only with a real microphone.
    r.in.wait_confirmed = false;
    f = step(&r, 500);
    NB_CHECK_EQ_INT(NB_GESTURE_NONE, f.gesture);
    NB_CHECK_EQ_INT(NB_ACTIVITY_BARS, f.activity);
    r.in.listening = false;
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, step(&r, 600).activity);
}

static void test_cheeks_stay_with_the_hand_and_the_hearts(void) {
    rig_t r;
    rig_init(&r);
    // The cheeks belong to the pose, not to the gesture channel: a confirmed wait
    // puts the hand in G and leaves them on the embarrassed face.
    r.in.winner = NB_EXPR_EMBARRASSED;
    r.in.wait_confirmed = true;
    step(&r, 0);
    nb_overlay_frame_t f = step(&r, 400);
    NB_CHECK_EQ_INT(NB_GESTURE_HAND, f.gesture);
    NB_CHECK(f.cheeks);
    r.in.wait_confirmed = false;
    r.in.winner = NB_EXPR_LOVING;
    f = step(&r, 500);
    NB_CHECK_EQ_INT(NB_GESTURE_HEARTS, f.gesture);
    NB_CHECK(f.cheeks);
}

static void test_a_thinking_expression_never_invents_a_wait(void) {
    rig_t r;
    rig_init(&r);
    r.in.winner = NB_EXPR_THINKING;
    step(&r, 0);
    // The hand of the intention uses the thinking delay, not the wait's.
    NB_CHECK(step(&r, 500).hand == 0.0f);
}

static void test_bars_follow_the_level_irregularly(void) {
    rig_t r;
    rig_init(&r);
    r.in.listening = true;
    nb_overlay_frame_t quiet = step(&r, 0);
    for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
        NB_CHECK(fabsf(quiet.bars[i] - 0.12f) < 1e-4f);  // floor while silent
    }
    r.in.audio_level = 1.0f;
    nb_overlay_frame_t loud = quiet;
    for (uint32_t t = 0; t <= 400; t += 33) {
        loud = step(&r, t);
    }
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < NB_OVERLAY_BARS; ++i) {
        NB_CHECK(loud.bars[i] > 0.4f && loud.bars[i] <= 1.0f);
        lo = fminf(lo, loud.bars[i]);
        hi = fmaxf(hi, loud.bars[i]);
    }
    NB_CHECK(hi - lo > 0.1f);  // not a flat row
}

static void test_level_falls_slower_than_it_rises(void) {
    rig_t up, down;
    rig_init(&up);
    rig_init(&down);
    up.in.listening = down.in.listening = true;
    up.in.audio_level = 0.0f;
    step(&up, 0);
    up.in.audio_level = 1.0f;
    step(&up, 40);
    down.in.audio_level = 1.0f;
    step(&down, 0);
    down.core.level = 1.0f;
    down.in.audio_level = 0.0f;
    step(&down, 40);
    NB_CHECK(up.core.level > 1.0f - down.core.level);
}

static void test_hearts_take_turns_rising_and_cheeks_are_pose(void) {
    rig_t r;
    rig_init(&r);
    r.in.winner = NB_EXPR_LOVING;
    step(&r, 0);
    nb_overlay_frame_t f = step(&r, 600);
    NB_CHECK_EQ_INT(NB_GESTURE_HEARTS, f.gesture);
    NB_CHECK(f.cheeks);
    NB_CHECK(fabsf(f.hearts[0] - 0.25f) < 1e-4f);
    NB_CHECK(fabsf(f.hearts[1] - 0.75f) < 1e-4f);
    // Embarrassed has cheeks and no gesture.
    r.in.winner = NB_EXPR_EMBARRASSED;
    f = step(&r, 700);
    NB_CHECK(f.cheeks);
    NB_CHECK_EQ_INT(NB_GESTURE_NONE, f.gesture);
}

static void test_null_is_safe(void) {
    nb_overlay_frame_t f;
    nb_overlay_input_t in;
    memset(&in, 0, sizeof(in));
    nb_overlay_init(NULL);
    nb_overlay_update(NULL, &in, 0, &f);
    nb_overlay_t o;
    nb_overlay_init(&o);
    nb_overlay_update(&o, NULL, 0, &f);
    nb_overlay_update(&o, &in, 0, NULL);
}

static void test_sleep_shows_rising_zs_and_wins_over_the_other_gestures(void) {
    rig_t r;
    rig_init(&r);
    r.in.sleeping = true;
    r.in.winner = NB_EXPR_SLEEPY;
    nb_overlay_frame_t f = step(&r, 1000);
    NB_CHECK_EQ_INT(NB_GESTURE_ZS, f.gesture);
    NB_CHECK(f.zs[1] == 0.0f && f.zs[2] == 0.0f);  // they start one by one
    // Later all three are up, each at its own height, and they keep cycling.
    f = step(&r, 1000 + 2500);
    NB_CHECK(f.zs[0] > f.zs[1] && f.zs[1] > f.zs[2] && f.zs[2] > 0.0f);
    for (int i = 0; i < NB_OVERLAY_ZS; ++i) {
        NB_CHECK(f.zs[i] >= 0.0f && f.zs[i] < 1.0f);
    }
    // Sleep beats a confirmed wait and a thinking intention in G.
    r.in.wait_confirmed = true;
    r.in.winner = NB_EXPR_THINKING;
    NB_CHECK_EQ_INT(NB_GESTURE_ZS, step(&r, 4000).gesture);
    // The voice still shows in A (the wake word answer) while Zs stay in G.
    r.in.speaking = true;
    f = step(&r, 4100);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, f.activity);
    NB_CHECK_EQ_INT(NB_GESTURE_ZS, f.gesture);
    // Waking ends the Zs.
    r.in.sleeping = false;
    r.in.wait_confirmed = false;
    r.in.speaking = false;
    r.in.winner = NB_EXPR_NEUTRAL;
    step(&r, 5000);  // the bubble lingers
    f = step(&r, 6000);
    NB_CHECK_EQ_INT(NB_GESTURE_NONE, f.gesture);
    NB_CHECK_EQ_INT(NB_ACTIVITY_NONE, f.activity);
    // Drowsy but awake: no Zs.
    r.in.winner = NB_EXPR_SLEEPY;
    NB_CHECK_EQ_INT(NB_GESTURE_NONE, step(&r, 7000).gesture);
}

static void test_technical_channel_is_independent_and_safety_wins(void) {
    rig_t r;
    rig_init(&r);
    r.in.net_down = true;
    NB_CHECK_EQ_INT(NB_TECH_NET, step(&r, 0).tech);
    r.in.safety_stop = true;
    NB_CHECK_EQ_INT(NB_TECH_SAFETY, step(&r, 33).tech);
    // It coexists with the other channels and with sleep.
    r.in.sleeping = true;
    r.in.speaking = true;
    nb_overlay_frame_t f = step(&r, 66);
    NB_CHECK_EQ_INT(NB_TECH_SAFETY, f.tech);
    NB_CHECK_EQ_INT(NB_ACTIVITY_SPEECH, f.activity);
    NB_CHECK_EQ_INT(NB_GESTURE_ZS, f.gesture);
    // When the stop ends the network mark returns if the condition remains.
    r.in.safety_stop = false;
    NB_CHECK_EQ_INT(NB_TECH_NET, step(&r, 99).tech);
    r.in.net_down = false;
    NB_CHECK_EQ_INT(NB_TECH_NONE, step(&r, 132).tech);
}

int main(void) {
    NB_RUN(test_rest_shows_nothing);
    NB_RUN(test_bars_follow_real_listening_not_the_expression);
    NB_RUN(test_bars_open_at_first_then_close_when_the_person_goes_quiet);
    NB_RUN(test_bars_give_up_if_the_person_never_speaks);
    NB_RUN(test_a_new_listening_spell_opens_the_bars_again);
    NB_RUN(test_speaking_wins_over_listening_and_the_expression);
    NB_RUN(test_bubble_pops_then_settles);
    NB_RUN(test_bubble_lingers_then_leaves);
    NB_RUN(test_voice_back_during_linger_keeps_the_bubble);
    NB_RUN(test_thinking_hand_waits_then_slides_in);
    NB_RUN(test_thinking_during_listening_keeps_the_bars_and_no_hand);
    NB_RUN(test_confirmed_wait_replaces_the_bars_with_the_hand_at_once);
    NB_RUN(test_cheeks_stay_with_the_hand_and_the_hearts);
    NB_RUN(test_a_thinking_expression_never_invents_a_wait);
    NB_RUN(test_bars_follow_the_level_irregularly);
    NB_RUN(test_level_falls_slower_than_it_rises);
    NB_RUN(test_hearts_take_turns_rising_and_cheeks_are_pose);
    NB_RUN(test_null_is_safe);
    NB_RUN(test_sleep_shows_rising_zs_and_wins_over_the_other_gestures);
    NB_RUN(test_technical_channel_is_independent_and_safety_wins);
    NB_TEST_MAIN_END();
}
