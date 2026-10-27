#include <math.h>

#include "nb_avatar.h"
#include "nb_test.h"

static void test_idle_uses_reference_silhouette(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 1u);
    nb_avatar_core_get_frame(&state, &frame);

    NB_CHECK(frame.enabled);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_IDLE);
    NB_CHECK(frame.pose.left_eye.width == 155.0f);
    NB_CHECK(frame.pose.left_eye.height == 360.0f);
    NB_CHECK(frame.pose.left_eye.x == -225.0f);
    NB_CHECK(frame.pose.right_eye.x == 225.0f);
}

static void test_catalog_resolves_every_expression(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 2u);
    for (int id = NB_AVATAR_EXPR_IDLE; id < NB_AVATAR_EXPR_COUNT; ++id) {
        NB_CHECK(nb_avatar_core_request(&state, (nb_avatar_expression_t)id));
        nb_avatar_core_tick(&state, 50u);
        nb_avatar_core_get_frame(&state, &frame);
        NB_CHECK(frame.enabled);
        NB_CHECK(frame.expression == (nb_avatar_expression_t)id);
        NB_CHECK(isfinite(frame.pose.left_eye.width));
        NB_CHECK(frame.pose.left_eye.width > 0.0f);
        NB_CHECK(nb_avatar_core_expression_name((nb_avatar_expression_t)id)[0] != '\0');
    }
    NB_CHECK(!nb_avatar_core_request(&state, NB_AVATAR_EXPR_COUNT));
}

static void test_persistent_expressions_loop(void)
{
    const nb_avatar_expression_t ids[] = {
        NB_AVATAR_EXPR_IDLE,
        NB_AVATAR_EXPR_LISTENING,
        NB_AVATAR_EXPR_THINKING,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        nb_avatar_core_state_t state;
        nb_avatar_render_frame_t frame;
        nb_avatar_core_init(&state, 3u);
        NB_CHECK(nb_avatar_core_request(&state, ids[i]));
        for (uint32_t elapsed = 0; elapsed < 30000u; elapsed += 37u) {
            nb_avatar_core_tick(&state, 37u);
        }
        nb_avatar_core_get_frame(&state, &frame);
        NB_CHECK(frame.expression == ids[i]);
        NB_CHECK(frame.pose.left_eye.width > 0.0f);
    }
}

static void test_once_expression_returns_to_current_base(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 4u);
    NB_CHECK(nb_avatar_core_request(&state, NB_AVATAR_EXPR_LISTENING));
    nb_avatar_core_tick(&state, 700u);
    NB_CHECK(nb_avatar_core_request(&state, NB_AVATAR_EXPR_SURPRISED));
    for (uint32_t elapsed = 0; elapsed < 8000u; elapsed += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_LISTENING);
}

static void test_gaze_target_moves_face_in_both_axes(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 5u);
    nb_avatar_core_set_gaze_target(&state, 1.0f, 1.0f, true);
    for (uint32_t elapsed = 0u; elapsed < 1200u; elapsed += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    const float right_center_x = (frame.pose.left_eye.x + frame.pose.right_eye.x) * 0.5f;
    const float down_center_y = (frame.pose.left_eye.y + frame.pose.right_eye.y) * 0.5f;
    NB_CHECK(right_center_x > 45.0f);
    NB_CHECK(down_center_y > 30.0f);

    nb_avatar_core_set_gaze_target(&state, -1.0f, -1.0f, true);
    for (uint32_t elapsed = 0u; elapsed < 1600u; elapsed += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    const float left_center_x = (frame.pose.left_eye.x + frame.pose.right_eye.x) * 0.5f;
    const float up_center_y = (frame.pose.left_eye.y + frame.pose.right_eye.y) * 0.5f;
    NB_CHECK(left_center_x < -45.0f);
    NB_CHECK(up_center_y < -30.0f);
}

static nb_avatar_render_frame_t look_frame(nb_avatar_core_state_t *state,
                                           nb_avatar_look_t look, bool active)
{
    nb_avatar_render_frame_t frame;
    nb_avatar_core_set_look(state, &look, active);
    for (uint32_t elapsed = 0u; elapsed < 1000u; elapsed += 20u) {
        nb_avatar_core_tick(state, 20u);
    }
    nb_avatar_core_get_frame(state, &frame);
    return frame;
}

static void test_idle_keeps_visible_2_5d_attention(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 6u);
    const nb_avatar_look_t center = {0, 0, 0, 0, 0};
    const nb_avatar_look_t look_up = {0, -26, 0, 0, -82};
    const nb_avatar_look_t look_down = {0, 26, 0, 0, 82};
    const nb_avatar_look_t look_side = {24, -10, 4.8f, 12, -5};
    const nb_avatar_render_frame_t early = look_frame(&state, center, true);
    const nb_avatar_render_frame_t up = look_frame(&state, look_up, true);
    const nb_avatar_render_frame_t down = look_frame(&state, look_down, true);
    const nb_avatar_render_frame_t side = look_frame(&state, look_side, true);

    const float early_center_y = (early.pose.left_eye.y + early.pose.right_eye.y) * 0.5f;
    const float up_center_y = (up.pose.left_eye.y + up.pose.right_eye.y) * 0.5f;
    const float down_center_y = (down.pose.left_eye.y + down.pose.right_eye.y) * 0.5f;
    NB_CHECK(up_center_y < early_center_y - 75.0f);
    NB_CHECK(down_center_y > early_center_y + 75.0f);
    NB_CHECK(fabsf(side.pose.left_eye.width - side.pose.right_eye.width) > 18.0f);
    NB_CHECK(fabsf((down.pose.left_eye.x + down.pose.right_eye.x) * 0.5f) < 5.0f);
    /* 2.5D pitch: besides moving in Y, the face comes closer, the eyes
     * compress slightly and tilt in opposite directions. */
    NB_CHECK(down.pose.left_eye.height < early.pose.left_eye.height * 0.94f);
    NB_CHECK(down.pose.face_scale > early.pose.face_scale * 1.04f);
    NB_CHECK(down.pose.left_eye.angle * down.pose.right_eye.angle < 0.0f);
}

static void test_turning_head_has_perspective(void)
{
    // Looking right on a turning head: the eyes move right, the left eye is
    // now nearer (larger) and the right one farther (smaller and narrower).
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 6u);
    const nb_avatar_render_frame_t rest = look_frame(&state, (nb_avatar_look_t){0, 0, 0, 0, 0}, true);
    const nb_avatar_render_frame_t right =
        look_frame(&state, (nb_avatar_look_t){27, 0, 0, 0, 0}, true);
    NB_CHECK(right.pose.right_eye.x > rest.pose.right_eye.x + 60.0f);
    NB_CHECK(right.pose.left_eye.x > rest.pose.left_eye.x + 100.0f);
    NB_CHECK(right.pose.left_eye.width > rest.pose.left_eye.width * 1.1f);
    NB_CHECK(right.pose.right_eye.width < rest.pose.right_eye.width * 0.75f);
    NB_CHECK(right.pose.right_eye.height < right.pose.left_eye.height);
    // Looking up squashes the eyes and tilts them the other way from down.
    const nb_avatar_render_frame_t up = look_frame(&state, (nb_avatar_look_t){0, -26, 0, 0, 0}, true);
    NB_CHECK(up.pose.left_eye.height < rest.pose.left_eye.height * 0.95f);
    NB_CHECK(up.pose.left_eye.angle > 0.5f && up.pose.right_eye.angle < -0.5f);
}

static void test_sleep_shuts_the_eyes_into_an_arc(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 6u);
    nb_avatar_core_hold(&state, NB_AVATAR_EXPR_SLEEPY);
    nb_avatar_render_frame_t frame;
    for (uint32_t t = 0u; t < 2000u; t += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    const float awake_height = frame.pose.left_eye.height;
    nb_avatar_core_set_eyes_closed(&state, true);
    // Halfway through closing: not shut yet.
    for (uint32_t t = 0u; t < NB_AVATAR_CLOSE_MS / 2u; t += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.pose.left_eye.crescent > -0.9f);
    // Shut: a thin lower arc, full height (no flattening), and no blinking
    // for a whole minute.
    float lowest_blink = 1.0f;
    for (uint32_t t = 0u; t < 60000u; t += 20u) {
        nb_avatar_core_tick(&state, 20u);
        nb_avatar_core_get_frame(&state, &frame);
        lowest_blink = fminf(lowest_blink, frame.blink_scale);
    }
    NB_CHECK(frame.pose.left_eye.crescent < -0.9f && frame.pose.right_eye.crescent < -0.9f);
    NB_CHECK(fabsf(frame.pose.left_eye.height - awake_height) < 1.0f);
    NB_CHECK(lowest_blink > 0.99f);
    // Waking opens them fast.
    nb_avatar_core_set_eyes_closed(&state, false);
    for (uint32_t t = 0u; t < NB_AVATAR_OPEN_MS + 40u; t += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.pose.left_eye.crescent > -0.8f);
}

static void test_look_fades_in_and_out(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 6u);
    const nb_avatar_look_t look_down = {0, 26, 0, 0, 82};
    nb_avatar_render_frame_t frame;
    nb_avatar_core_get_frame(&state, &frame);
    const float rest_y = frame.pose.left_eye.y;
    // Halfway through the fade the eyes are partway there: no jolt.
    nb_avatar_core_set_look(&state, &look_down, true);
    for (uint32_t t = 0u; t < NB_AVATAR_LOOK_FADE_MS / 2u; t += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    nb_avatar_core_state_t full_state;
    nb_avatar_core_init(&full_state, 6u);
    const float full = look_frame(&full_state, look_down, true).pose.left_eye.y - rest_y;
    NB_CHECK(frame.pose.left_eye.y > rest_y + full * 0.25f);
    NB_CHECK(frame.pose.left_eye.y < rest_y + full * 0.75f);
    // A reaction turns the wandering off: the eyes come back to the center.
    const nb_avatar_render_frame_t back = look_frame(&state, look_down, false);
    NB_CHECK(fabsf(back.pose.left_eye.y - rest_y) < 10.0f);
}

static void test_blink_pace_slows_blinks(void)
{
    // Count blinks over a minute at each pace.
    int counts[2];
    const float paces[2] = {0.8f, 1.6f};
    for (int i = 0; i < 2; i++) {
        nb_avatar_core_state_t state;
        nb_avatar_core_init(&state, 9u);
        nb_avatar_core_set_blink_pace(&state, paces[i]);
        counts[i] = 0;
        bool closed = false;
        for (uint32_t t = 0u; t < 60000u; t += 10u) {
            nb_avatar_core_tick(&state, 10u);
            nb_avatar_render_frame_t frame;
            nb_avatar_core_get_frame(&state, &frame);
            const bool now_closed = frame.blink_scale < 0.5f;
            counts[i] += now_closed && !closed;
            closed = now_closed;
        }
    }
    NB_CHECK(counts[0] > counts[1] + 3);
    NB_CHECK(counts[1] >= 5);
}

static void test_external_blink_modulates_without_replacing_expression(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 7u);
    NB_CHECK(nb_avatar_core_request(&state, NB_AVATAR_EXPR_LISTENING));
    nb_avatar_core_set_blink_override(&state, 0.35f, true);
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_LISTENING);
    NB_CHECK(fabsf(frame.blink_scale - 0.35f) < 0.0001f);
    nb_avatar_core_set_blink_override(&state, 0.0f, false);
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(fabsf(frame.blink_scale - 1.0f) < 0.0001f);
}

static void test_sleepy_holds_until_product_state_requests_idle(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 8u);
    NB_CHECK(nb_avatar_core_request(&state, NB_AVATAR_EXPR_LISTENING));
    NB_CHECK(nb_avatar_core_play(&state, NB_AVATAR_EXPR_SLEEPY, NB_AVATAR_PLAY_ONCE, false));
    for (uint32_t elapsed = 0u; elapsed < 8000u; elapsed += 20u) {
        nb_avatar_core_tick(&state, 20u);
    }
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_SLEEPY);
    NB_CHECK(nb_avatar_core_is_once_active(&state));

    NB_CHECK(nb_avatar_core_request(&state, NB_AVATAR_EXPR_IDLE));
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_IDLE);
    NB_CHECK(!nb_avatar_core_is_once_active(&state));
}

static void test_pingpong_keeps_expression_active(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    bool reversed = false;
    nb_avatar_core_init(&state, 6u);
    NB_CHECK(nb_avatar_core_play(&state, NB_AVATAR_EXPR_CURIOUS, NB_AVATAR_PLAY_PINGPONG, false));
    for (uint32_t elapsed = 0u; elapsed < 10000u; elapsed += 20u) {
        nb_avatar_core_tick(&state, 20u);
        if (state.playback_direction < 0) {
            reversed = true;
        }
    }
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(reversed);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_CURIOUS);
    NB_CHECK(state.playback == NB_AVATAR_PLAY_PINGPONG);
}

// Peak-to-peak of the frame's vertical offset over window_ms.
static float face_y_swing(nb_avatar_core_state_t *state, uint32_t window_ms)
{
    nb_avatar_render_frame_t frame;
    float lo = 1e9f;
    float hi = -1e9f;
    for (uint32_t elapsed = 0u; elapsed < window_ms; elapsed += 10u) {
        nb_avatar_core_tick(state, 10u);
        nb_avatar_core_get_frame(state, &frame);
        lo = fminf(lo, frame.pose.face_y);
        hi = fmaxf(hi, frame.pose.face_y);
    }
    return hi - lo;
}

static void tick_for(nb_avatar_core_state_t *state, uint32_t ms)
{
    for (uint32_t elapsed = 0u; elapsed < ms; elapsed += 20u) {
        nb_avatar_core_tick(state, 20u);
    }
}

static void test_hold_keeps_once_expression_and_settles_motion(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 11u);
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_HAPPY));
    nb_avatar_core_tick(&state, 300u);
    const float playing = face_y_swing(&state, 800u);

    tick_for(&state, 58000u);
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_HAPPY);
    NB_CHECK(state.holding);
    const float held = face_y_swing(&state, 1000u);
    NB_CHECK(held > 0.0f);
    NB_CHECK(held < playing * 0.6f);
}

static void test_hold_same_expression_does_not_restart(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 12u);
    NB_CHECK(nb_avatar_core_request(&state, NB_AVATAR_EXPR_ANGRY));
    nb_avatar_core_tick(&state, 300u);
    const uint32_t started = state.expression_started_ms;
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_ANGRY));
    NB_CHECK_EQ_INT(started, state.expression_started_ms);
    NB_CHECK(state.holding);
    NB_CHECK(!state.return_to_base);
}

static void test_hold_persistent_behaves_as_request(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 13u);
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_LISTENING));
    NB_CHECK(state.base_expression == NB_AVATAR_EXPR_LISTENING);
    NB_CHECK(state.playback == NB_AVATAR_PLAY_LOOP);
    NB_CHECK(!state.holding);
}

static void test_release_finishes_first_pass_then_returns(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 14u);
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_HAPPY));
    nb_avatar_core_tick(&state, 500u);
    const uint32_t ends = state.expression_ends_ms;
    NB_CHECK(nb_avatar_core_release(&state, NB_AVATAR_EXPR_IDLE));

    tick_for(&state, ends - state.now_ms - 40u);
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_HAPPY);

    tick_for(&state, 200u);
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_IDLE);
}

static void test_release_after_pass_returns_at_once(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 15u);
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_SAD));
    tick_for(&state, 10000u);
    NB_CHECK(nb_avatar_core_release(&state, NB_AVATAR_EXPR_IDLE));
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == NB_AVATAR_EXPR_IDLE);
    NB_CHECK(!state.holding);
}

static void test_release_needs_persistent_base(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_core_init(&state, 16u);
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_HAPPY));
    NB_CHECK(!nb_avatar_core_release(&state, NB_AVATAR_EXPR_SAD));
    NB_CHECK(state.holding);
    NB_CHECK(!nb_avatar_core_release(NULL, NB_AVATAR_EXPR_IDLE));
    NB_CHECK(!nb_avatar_core_hold(NULL, NB_AVATAR_EXPR_HAPPY));
    NB_CHECK(!nb_avatar_core_hold(&state, NB_AVATAR_EXPR_COUNT));
}

// The V2 pose an expression holds once its first pass is over.
static nb_avatar_render_frame_t held_frame(nb_avatar_expression_t expression)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 17u);
    NB_CHECK(nb_avatar_core_hold(&state, expression));
    tick_for(&state, 6000u);
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.expression == expression);
    return frame;
}

static void test_v2_arcs_are_crescents(void)
{
    const nb_avatar_render_frame_t happy = held_frame(NB_AVATAR_EXPR_HAPPY);
    const nb_avatar_render_frame_t loving = held_frame(NB_AVATAR_EXPR_LOVING);
    const nb_avatar_render_frame_t sleepy = held_frame(NB_AVATAR_EXPR_SLEEPY);
    // Happy and loving close from below; loving is the thinner arc.
    NB_CHECK(happy.pose.left_eye.crescent > 0.6f);
    NB_CHECK(happy.pose.right_eye.crescent > 0.6f);
    NB_CHECK(loving.pose.left_eye.crescent > happy.pose.left_eye.crescent);
    // Sleepy closes from above.
    NB_CHECK(sleepy.pose.left_eye.crescent < -0.6f);
    // A plain capsule has no cut.
    const nb_avatar_render_frame_t idle = held_frame(NB_AVATAR_EXPR_IDLE);
    NB_CHECK(idle.pose.left_eye.crescent == 0.0f);
}

static void test_v2_half_moons_slant_the_right_way(void)
{
    // Positive tilt drops the lid on the right side of that eye.
    const nb_avatar_render_frame_t angry = held_frame(NB_AVATAR_EXPR_ANGRY);
    NB_CHECK(angry.pose.left_eye.upper_lid_tilt > 0.5f);   // low toward the middle
    NB_CHECK(angry.pose.right_eye.upper_lid_tilt < -0.5f);
    const nb_avatar_render_frame_t sad = held_frame(NB_AVATAR_EXPR_SAD);
    NB_CHECK(sad.pose.left_eye.upper_lid_tilt < -0.5f);    // low toward the outside
    NB_CHECK(sad.pose.right_eye.upper_lid_tilt > 0.5f);
    const nb_avatar_render_frame_t shy = held_frame(NB_AVATAR_EXPR_EMBARRASSED);
    NB_CHECK(shy.pose.left_eye.upper_lid > 0.25f);
    NB_CHECK(shy.pose.left_eye.y > 0.0f);                  // looking down
    const nb_avatar_render_frame_t confused = held_frame(NB_AVATAR_EXPR_CONFUSED);
    NB_CHECK(confused.pose.left_eye.upper_lid > 0.4f);     // one half-moon
    NB_CHECK(confused.pose.right_eye.upper_lid < 0.01f);   // one capsule
}

static void test_v2_has_no_brows(void)
{
    for (int id = NB_AVATAR_EXPR_IDLE; id < NB_AVATAR_EXPR_COUNT; ++id) {
        if (id == NB_AVATAR_EXPR_DIZZY) {
            continue;
        }
        const nb_avatar_render_frame_t frame = held_frame((nb_avatar_expression_t)id);
        NB_CHECK(frame.pose.left_eye.brow_opacity == 0.0f);
        NB_CHECK(frame.pose.right_eye.brow_opacity == 0.0f);
    }
}

static void test_crescent_interpolates_on_the_way_in(void)
{
    nb_avatar_core_state_t state;
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(&state, 18u);
    NB_CHECK(nb_avatar_core_hold(&state, NB_AVATAR_EXPR_HAPPY));
    tick_for(&state, 120u);  // 5 ms into the move from the pop (115 ms) to the first arc
    nb_avatar_core_get_frame(&state, &frame);
    NB_CHECK(frame.pose.left_eye.crescent > 0.0f);
    NB_CHECK(frame.pose.left_eye.crescent < 0.7f);
}

static void test_null_calls_are_safe(void)
{
    nb_avatar_render_frame_t frame;
    nb_avatar_core_init(NULL, 0u);
    nb_avatar_core_tick(NULL, 10u);
    NB_CHECK(!nb_avatar_core_request(NULL, NB_AVATAR_EXPR_IDLE));
    NB_CHECK(!nb_avatar_core_play(NULL, NB_AVATAR_EXPR_IDLE, NB_AVATAR_PLAY_LOOP, false));
    NB_CHECK(!nb_avatar_core_play(NULL, NB_AVATAR_EXPR_IDLE,
                               (nb_avatar_playback_t)(NB_AVATAR_PLAY_PINGPONG + 1), false));
    nb_avatar_core_set_gaze_target(NULL, 1.0f, 1.0f, true);
    nb_avatar_core_set_blink_override(NULL, 0.0f, true);
    nb_avatar_core_get_frame(NULL, &frame);
    NB_CHECK(!frame.enabled);
    nb_avatar_core_get_frame(NULL, NULL);
}

int main(void) {
    NB_RUN(test_idle_uses_reference_silhouette);
    NB_RUN(test_catalog_resolves_every_expression);
    NB_RUN(test_persistent_expressions_loop);
    NB_RUN(test_once_expression_returns_to_current_base);
    NB_RUN(test_gaze_target_moves_face_in_both_axes);
    NB_RUN(test_idle_keeps_visible_2_5d_attention);
    NB_RUN(test_turning_head_has_perspective);
    NB_RUN(test_sleep_shuts_the_eyes_into_an_arc);
    NB_RUN(test_look_fades_in_and_out);
    NB_RUN(test_blink_pace_slows_blinks);
    NB_RUN(test_external_blink_modulates_without_replacing_expression);
    NB_RUN(test_sleepy_holds_until_product_state_requests_idle);
    NB_RUN(test_pingpong_keeps_expression_active);
    NB_RUN(test_hold_keeps_once_expression_and_settles_motion);
    NB_RUN(test_hold_same_expression_does_not_restart);
    NB_RUN(test_hold_persistent_behaves_as_request);
    NB_RUN(test_release_finishes_first_pass_then_returns);
    NB_RUN(test_release_after_pass_returns_at_once);
    NB_RUN(test_release_needs_persistent_base);
    NB_RUN(test_v2_arcs_are_crescents);
    NB_RUN(test_v2_half_moons_slant_the_right_way);
    NB_RUN(test_v2_has_no_brows);
    NB_RUN(test_crescent_interpolates_on_the_way_in);
    NB_RUN(test_null_calls_are_safe);
    NB_TEST_MAIN_END();
}
