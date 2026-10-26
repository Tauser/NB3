#include "nb_motion_safety.h"
#include "nb_test.h"

#define PAN NB_MOTION_SAFETY_AXIS_PAN
#define TILT NB_MOTION_SAFETY_AXIS_TILT

static nb_motion_safety_feedback_t good_fb(void) {
    nb_motion_safety_feedback_t fb = {512, 100, 40, 5000};
    return fb;
}

static void feed_both(nb_motion_safety_t *ms, uint32_t now_ms) {
    nb_motion_safety_feedback_t fb = good_fb();
    nb_motion_safety_update(ms, PAN, &fb, now_ms);
    nb_motion_safety_update(ms, TILT, &fb, now_ms);
}

// Armed instance at now_ms == 0 with healthy feedback on both axes.
static void make_armed(nb_motion_safety_t *ms) {
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_init(ms, &cfg);
    feed_both(ms, 0);
    nb_motion_safety_arm(ms, 0);
}

// Same, with the feedback watchdog effectively off: for tests about heartbeat
// or tracking that let time pass without feeding every axis.
static void make_armed_no_feedback_watch(nb_motion_safety_t *ms) {
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    cfg.feedback_timeout_ms = 0x7FFFFFFFu;
    nb_motion_safety_init(ms, &cfg);
    feed_both(ms, 0);
    nb_motion_safety_arm(ms, 0);
}

static void expect_fault(const nb_motion_safety_t *ms, nb_motion_safety_fault_t reason,
                         nb_motion_safety_axis_t axis) {
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_FAULT, nb_motion_safety_get_state(ms));
    NB_CHECK_EQ_INT(reason, nb_motion_safety_get_fault(ms));
    NB_CHECK_EQ_INT(axis, nb_motion_safety_get_fault_axis(ms));
    NB_CHECK(!nb_motion_safety_can_move(ms));
}

static void test_starts_in_init_and_cannot_move(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    NB_CHECK(nb_motion_safety_init(&ms, &cfg));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_INIT, nb_motion_safety_get_state(&ms));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_NONE, nb_motion_safety_get_fault(&ms));
    NB_CHECK(!nb_motion_safety_can_move(&ms));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_STATE,
                    nb_motion_safety_command(&ms, PAN, 512, 0));
}

static void test_arm_needs_feedback_on_every_axis(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_feedback_t fb = good_fb();
    nb_motion_safety_init(&ms, &cfg);
    NB_CHECK(!nb_motion_safety_arm(&ms, 0));
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    NB_CHECK(!nb_motion_safety_arm(&ms, 0));
    nb_motion_safety_update(&ms, TILT, &fb, 0);
    NB_CHECK(nb_motion_safety_arm(&ms, 0));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_OK, nb_motion_safety_get_state(&ms));
    NB_CHECK(nb_motion_safety_can_move(&ms));
    NB_CHECK(!nb_motion_safety_arm(&ms, 1));  // already armed
}

static void test_command_accepts_in_range_and_refuses_outside(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK, nb_motion_safety_command(&ms, PAN, 512, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK, nb_motion_safety_command(&ms, PAN, 410, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK, nb_motion_safety_command(&ms, PAN, 614, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_RANGE, nb_motion_safety_command(&ms, PAN, 409, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_RANGE, nb_motion_safety_command(&ms, PAN, 615, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK, nb_motion_safety_command(&ms, TILT, 461, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_RANGE, nb_motion_safety_command(&ms, TILT, 460, 1));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_RANGE, nb_motion_safety_command(&ms, TILT, 564, 1));
    // A refused range is not a fault.
    NB_CHECK(nb_motion_safety_can_move(&ms));
}

static void test_clamp_position(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    NB_CHECK_EQ_INT(410, nb_motion_safety_clamp_position(&ms, PAN, 0));
    NB_CHECK_EQ_INT(614, nb_motion_safety_clamp_position(&ms, PAN, 1023));
    NB_CHECK_EQ_INT(500, nb_motion_safety_clamp_position(&ms, PAN, 500));
    NB_CHECK_EQ_INT(461, nb_motion_safety_clamp_position(&ms, TILT, 100));
    NB_CHECK_EQ_INT(563, nb_motion_safety_clamp_position(&ms, TILT, 900));
    // Whatever the clamp returns is accepted by the veto.
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK,
                    nb_motion_safety_command(&ms, PAN, nb_motion_safety_clamp_position(&ms, PAN, 0), 1));
}

static void test_over_temperature_faults(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.temp_c = 69;
    NB_CHECK(nb_motion_safety_update(&ms, TILT, &fb, 10));
    fb.temp_c = 70;
    NB_CHECK(!nb_motion_safety_update(&ms, TILT, &fb, 20));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, TILT);
}

static void test_brownout_faults(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.voltage_mv = 4500;  // the floor itself is still fine
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 10));
    fb.voltage_mv = 4499;
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 20));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, PAN);
}

static void test_unreadable_voltage_counts_as_brownout(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.voltage_mv = 0;
    NB_CHECK(!nb_motion_safety_update(&ms, TILT, &fb, 10));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, TILT);
}

static void test_over_voltage_faults(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.voltage_mv = 6500;
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 10));
    fb.voltage_mv = 6501;
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 20));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_OVER_VOLTAGE, PAN);
}

static void test_sustained_load_faults_after_stall_time(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.load = 800;
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 1000));  // timer starts
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 1099));
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 1100));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_STALL_LOAD, PAN);
}

static void test_load_spike_that_drops_does_not_fault(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.load = 800;
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    fb.load = 100;
    nb_motion_safety_update(&ms, PAN, &fb, 50);
    fb.load = 800;
    nb_motion_safety_update(&ms, PAN, &fb, 60);
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 150));  // only 90 ms since 60
    NB_CHECK(nb_motion_safety_can_move(&ms));
}

static void test_load_timer_is_wrap_safe(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.load = 800;
    nb_motion_safety_update(&ms, PAN, &fb, 0xFFFFFFC0u);
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 0x00000023u));  // 99 ms later
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 0x00000024u));  // 100 ms
}

static void test_goal_not_reached_faults_after_timeout(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.position = 450;
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK, nb_motion_safety_command(&ms, PAN, 500, 0));
    nb_motion_safety_heartbeat(&ms, 0);
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 0));
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 1499));
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 1500));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_STALL_TRACKING, PAN);
}

static void test_arriving_at_goal_clears_tracking_timer(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    nb_motion_safety_command(&ms, PAN, 500, 0);
    fb.position = 450;
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    fb.position = 490;  // within tolerance (20)
    nb_motion_safety_update(&ms, PAN, &fb, 1000);
    fb.position = 450;  // timer restarts here
    nb_motion_safety_update(&ms, PAN, &fb, 2000);
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 3400));
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 3500));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_STALL_TRACKING, nb_motion_safety_get_fault(&ms));
}

static void test_small_goal_steps_do_not_hide_a_stalled_servo(void) {
    nb_motion_safety_t ms;
    make_armed_no_feedback_watch(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.position = 450;
    nb_motion_safety_command(&ms, PAN, 500, 0);
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    nb_motion_safety_heartbeat(&ms, 700);
    nb_motion_safety_command(&ms, PAN, 505, 700);  // small step: timer keeps running
    nb_motion_safety_heartbeat(&ms, 1400);
    nb_motion_safety_command(&ms, PAN, 510, 1400);
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 1500));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_STALL_TRACKING, nb_motion_safety_get_fault(&ms));
}

static void test_large_goal_jump_restarts_tracking_window(void) {
    nb_motion_safety_t ms;
    make_armed_no_feedback_watch(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.position = 450;
    nb_motion_safety_command(&ms, PAN, 500, 0);
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    nb_motion_safety_heartbeat(&ms, 1400);
    nb_motion_safety_command(&ms, PAN, 600, 1400);  // new destination
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 1500));
    NB_CHECK(nb_motion_safety_update(&ms, PAN, &fb, 2999));
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, &fb, 3000));
}

static void test_heartbeat_timeout_faults(void) {
    nb_motion_safety_t ms;
    make_armed_no_feedback_watch(&ms);
    nb_motion_safety_heartbeat(&ms, 100);
    nb_motion_safety_tick(&ms, 699);
    NB_CHECK(nb_motion_safety_can_move(&ms));
    nb_motion_safety_tick(&ms, 700);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_HEARTBEAT, PAN);
}

static void test_heartbeat_refreshes_the_deadline(void) {
    nb_motion_safety_t ms;
    make_armed_no_feedback_watch(&ms);
    for (uint32_t t = 100; t <= 5000; t += 100) {
        nb_motion_safety_heartbeat(&ms, t);
        nb_motion_safety_tick(&ms, t);
    }
    NB_CHECK(nb_motion_safety_can_move(&ms));
}

static void test_arm_counts_as_first_heartbeat(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    cfg.feedback_timeout_ms = 0x7FFFFFFFu;
    nb_motion_safety_init(&ms, &cfg);
    feed_both(&ms, 5000);
    NB_CHECK(nb_motion_safety_arm(&ms, 5000));
    nb_motion_safety_tick(&ms, 5599);
    NB_CHECK(nb_motion_safety_can_move(&ms));
    nb_motion_safety_tick(&ms, 5600);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_HEARTBEAT, nb_motion_safety_get_fault(&ms));
}

static void test_heartbeat_timeout_across_clock_wraparound(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    cfg.feedback_timeout_ms = 0x7FFFFFFFu;
    nb_motion_safety_init(&ms, &cfg);
    feed_both(&ms, 0xFFFFFF00u);
    nb_motion_safety_arm(&ms, 0xFFFFFF00u);
    nb_motion_safety_tick(&ms, 0xFFFFFF00u + 599u);  // wraps to 0x147
    NB_CHECK(nb_motion_safety_can_move(&ms));
    nb_motion_safety_tick(&ms, 0xFFFFFF00u + 600u);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_HEARTBEAT, nb_motion_safety_get_fault(&ms));
}

static void test_stale_now_does_not_fault_heartbeat(void) {
    nb_motion_safety_t ms;
    make_armed_no_feedback_watch(&ms);
    nb_motion_safety_heartbeat(&ms, 8600);
    nb_motion_safety_tick(&ms, 8350);  // sampled before a slow read
    NB_CHECK(nb_motion_safety_can_move(&ms));
}

static void test_command_catches_stale_heartbeat_without_tick(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_STATE,
                    nb_motion_safety_command(&ms, PAN, 512, 10000));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_HEARTBEAT, nb_motion_safety_get_fault(&ms));
}

static void test_heartbeat_not_enforced_before_arm(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_init(&ms, &cfg);
    nb_motion_safety_tick(&ms, 100000);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_INIT, nb_motion_safety_get_state(&ms));
}

static void test_feedback_lost_faults_per_axis_after_timeout(void) {
    for (int axis = 0; axis < NB_MOTION_SAFETY_AXIS_COUNT; axis++) {
        nb_motion_safety_t ms;
        make_armed(&ms);
        nb_motion_safety_feedback_t fb = good_fb();
        nb_motion_safety_axis_t alive = axis == PAN ? TILT : PAN;
        nb_motion_safety_heartbeat(&ms, 299);
        nb_motion_safety_update(&ms, alive, &fb, 299);
        nb_motion_safety_tick(&ms, 299);
        NB_CHECK(nb_motion_safety_can_move(&ms));
        nb_motion_safety_heartbeat(&ms, 300);
        nb_motion_safety_update(&ms, alive, &fb, 300);
        nb_motion_safety_tick(&ms, 300);
        expect_fault(&ms, NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, (nb_motion_safety_axis_t)axis);
    }
}

static void test_feedback_refresh_keeps_axes_alive(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    for (uint32_t t = 50; t <= 3000; t += 50) {
        feed_both(&ms, t);
        nb_motion_safety_heartbeat(&ms, t);
        nb_motion_safety_tick(&ms, t);
    }
    NB_CHECK(nb_motion_safety_can_move(&ms));
}

static void test_feedback_timeout_not_enforced_before_arm(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_init(&ms, &cfg);
    feed_both(&ms, 0);
    nb_motion_safety_tick(&ms, 100000);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_INIT, nb_motion_safety_get_state(&ms));
}

static void test_command_catches_stale_feedback_without_tick(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_heartbeat(&ms, 400);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_STATE, nb_motion_safety_command(&ms, PAN, 512, 400));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, nb_motion_safety_get_fault(&ms));
}

static void test_arm_refused_with_stale_feedback(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_init(&ms, &cfg);
    feed_both(&ms, 0);
    NB_CHECK(!nb_motion_safety_arm(&ms, 300));
    nb_motion_safety_feedback_t fb = good_fb();
    nb_motion_safety_update(&ms, PAN, &fb, 300);
    NB_CHECK(!nb_motion_safety_arm(&ms, 300));  // tilt still stale
    nb_motion_safety_update(&ms, TILT, &fb, 300);
    NB_CHECK(nb_motion_safety_arm(&ms, 300));
}

static void test_feedback_lost_reset_needs_fresh_feedback_on_every_axis(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    nb_motion_safety_heartbeat(&ms, 400);
    nb_motion_safety_tick(&ms, 400);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, PAN);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 400));
    nb_motion_safety_update(&ms, PAN, &fb, 410);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 420));  // tilt still silent
    nb_motion_safety_update(&ms, TILT, &fb, 430);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 711));  // pan went stale again
    nb_motion_safety_update(&ms, PAN, &fb, 720);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 725));
    NB_CHECK(!nb_motion_safety_arm(&ms, 725));  // reset still demands fresh feedback
    feed_both(&ms, 740);
    NB_CHECK(nb_motion_safety_arm(&ms, 740));
}

static void test_feedback_timeout_across_clock_wraparound(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_init(&ms, &cfg);
    feed_both(&ms, 0xFFFFFF00u);
    nb_motion_safety_arm(&ms, 0xFFFFFF00u);
    nb_motion_safety_heartbeat(&ms, 0xFFFFFF00u + 299u);
    nb_motion_safety_tick(&ms, 0xFFFFFF00u + 299u);  // wraps past zero
    NB_CHECK(nb_motion_safety_can_move(&ms));
    nb_motion_safety_heartbeat(&ms, 0xFFFFFF00u + 300u);
    nb_motion_safety_tick(&ms, 0xFFFFFF00u + 300u);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, nb_motion_safety_get_fault(&ms));
}

static void test_fault_is_sticky_and_refuses_everything(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t hot = good_fb();
    hot.temp_c = 90;
    nb_motion_safety_update(&ms, PAN, &hot, 10);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, PAN);

    // Healthy data, heartbeats, and time passing never clear it.
    feed_both(&ms, 20);
    nb_motion_safety_heartbeat(&ms, 20);
    nb_motion_safety_tick(&ms, 30);
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_STATE, nb_motion_safety_command(&ms, PAN, 512, 30));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_STATE, nb_motion_safety_command(&ms, TILT, 512, 30));
    NB_CHECK(!nb_motion_safety_arm(&ms, 30));
    NB_CHECK(!nb_motion_safety_update(&ms, TILT, &(nb_motion_safety_feedback_t){512, 0, 30, 5000}, 40));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, PAN);
}

static void test_first_fault_reason_is_kept(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, TILT);
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, PAN);
    nb_motion_safety_tick(&ms, 100000);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, TILT);
}

static void test_fault_in_init_also_latches(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_feedback_t fb = good_fb();
    nb_motion_safety_init(&ms, &cfg);
    fb.voltage_mv = 3000;
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, PAN);
}

static void test_reset_refused_while_temperature_persists(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.temp_c = 80;
    nb_motion_safety_update(&ms, PAN, &fb, 10);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 20));
    nb_motion_safety_update(&ms, PAN, &fb, 30);  // still hot
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 40));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, PAN);

    fb.temp_c = 45;  // cooled
    nb_motion_safety_update(&ms, PAN, &fb, 50);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 60));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_INIT, nb_motion_safety_get_state(&ms));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_NONE, nb_motion_safety_get_fault(&ms));
}

static void test_reset_refused_while_voltage_persists(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.voltage_mv = 4000;
    nb_motion_safety_update(&ms, TILT, &fb, 10);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 20));
    fb.voltage_mv = 5000;
    nb_motion_safety_update(&ms, TILT, &fb, 30);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 40));
}

static void test_reset_refused_while_overloaded_or_not_tracking(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_feedback_t fb = good_fb();
    fb.load = 900;
    nb_motion_safety_update(&ms, PAN, &fb, 0);
    nb_motion_safety_update(&ms, PAN, &fb, 200);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_STALL_LOAD, PAN);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 300));
    fb.load = 50;
    nb_motion_safety_update(&ms, PAN, &fb, 400);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 500));

    make_armed(&ms);
    fb = good_fb();
    fb.position = 450;
    nb_motion_safety_command(&ms, TILT, 500, 0);
    nb_motion_safety_update(&ms, TILT, &fb, 0);
    nb_motion_safety_update(&ms, TILT, &fb, 1500);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_STALL_TRACKING, TILT);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 1600));
    fb.position = 500;
    nb_motion_safety_update(&ms, TILT, &fb, 1700);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 1800));
}

static void test_heartbeat_fault_reset_needs_live_heartbeat(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_tick(&ms, 600);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_HEARTBEAT, PAN);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 700));  // owner still silent
    nb_motion_safety_heartbeat(&ms, 700);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 710));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_INIT, nb_motion_safety_get_state(&ms));
}

static void test_reset_is_only_valid_from_fault(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 0));
    NB_CHECK(nb_motion_safety_can_move(&ms));
}

static void test_after_reset_rearm_needs_fresh_feedback_and_goals_are_dropped(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_command(&ms, PAN, 600, 0);
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, PAN);
    feed_both(&ms, 10);
    NB_CHECK(nb_motion_safety_reset_fault(&ms, 20));
    NB_CHECK(!nb_motion_safety_arm(&ms, 20));  // feedback must be re-observed
    feed_both(&ms, 30);                        // position 512 vs old goal 600 is ignored
    NB_CHECK(nb_motion_safety_arm(&ms, 30));
    feed_both(&ms, 5000);
    nb_motion_safety_heartbeat(&ms, 5000);
    NB_CHECK(nb_motion_safety_can_move(&ms));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_OK, nb_motion_safety_command(&ms, PAN, 512, 5001));
}

static void test_trip_forces_fault_and_is_reset_when_no_condition(void) {
    nb_motion_safety_t ms;
    make_armed(&ms);
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_STALL_LOAD, TILT);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_STALL_LOAD, TILT);
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_NONE, PAN);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_STALL_LOAD, TILT);
}

static void test_invalid_config_starts_faulted_and_cannot_be_reset(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    cfg.pos_min[PAN] = 700;  // min above max
    NB_CHECK(!nb_motion_safety_init(&ms, &cfg));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_CONFIG, PAN);
    NB_CHECK(!nb_motion_safety_reset_fault(&ms, 0));
    NB_CHECK(!nb_motion_safety_arm(&ms, 0));

    cfg = nb_motion_safety_default_cfg();
    cfg.overvoltage_mv = cfg.brownout_mv;
    NB_CHECK(!nb_motion_safety_init(&ms, &cfg));
    cfg = nb_motion_safety_default_cfg();
    cfg.heartbeat_timeout_ms = 0;
    NB_CHECK(!nb_motion_safety_init(&ms, &cfg));
    cfg = nb_motion_safety_default_cfg();
    cfg.heartbeat_timeout_ms = 0x80000000u;
    NB_CHECK(!nb_motion_safety_init(&ms, &cfg));
    cfg = nb_motion_safety_default_cfg();
    cfg.feedback_timeout_ms = 0;
    NB_CHECK(!nb_motion_safety_init(&ms, &cfg));
    cfg.feedback_timeout_ms = 0x80000000u;
    NB_CHECK(!nb_motion_safety_init(&ms, &cfg));
    NB_CHECK(!nb_motion_safety_init(&ms, NULL));
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_CONFIG, PAN);
}

static void test_null_and_invalid_inputs_are_safe(void) {
    nb_motion_safety_t ms;
    nb_motion_safety_feedback_t fb = good_fb();
    make_armed(&ms);

    NB_CHECK(!nb_motion_safety_init(NULL, NULL));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_STATE_FAULT, nb_motion_safety_get_state(NULL));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_FAULT_CONFIG, nb_motion_safety_get_fault(NULL));
    NB_CHECK_EQ_INT(PAN, nb_motion_safety_get_fault_axis(NULL));
    NB_CHECK(!nb_motion_safety_can_move(NULL));
    NB_CHECK(!nb_motion_safety_arm(NULL, 0));
    NB_CHECK(!nb_motion_safety_update(NULL, PAN, &fb, 0));
    NB_CHECK(!nb_motion_safety_update(&ms, PAN, NULL, 0));
    NB_CHECK(!nb_motion_safety_update(&ms, NB_MOTION_SAFETY_AXIS_COUNT, &fb, 0));
    NB_CHECK(!nb_motion_safety_update(&ms, (nb_motion_safety_axis_t)-1, &fb, 0));
    nb_motion_safety_heartbeat(NULL, 0);
    nb_motion_safety_tick(NULL, 0);
    nb_motion_safety_trip(NULL, NB_MOTION_SAFETY_FAULT_BROWNOUT, PAN);
    NB_CHECK(!nb_motion_safety_reset_fault(NULL, 0));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_INVALID, nb_motion_safety_command(NULL, PAN, 512, 0));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_INVALID,
                    nb_motion_safety_command(&ms, NB_MOTION_SAFETY_AXIS_COUNT, 512, 0));
    NB_CHECK_EQ_INT(NB_MOTION_SAFETY_CMD_REFUSED_INVALID,
                    nb_motion_safety_command(&ms, (nb_motion_safety_axis_t)-1, 512, 0));
    NB_CHECK_EQ_INT(777, nb_motion_safety_clamp_position(NULL, PAN, 777));
    NB_CHECK_EQ_INT(777, nb_motion_safety_clamp_position(&ms, NB_MOTION_SAFETY_AXIS_COUNT, 777));

    // Bad arguments never disturb a healthy instance.
    NB_CHECK(nb_motion_safety_can_move(&ms));
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_NONE, PAN);
    nb_motion_safety_trip(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, (nb_motion_safety_axis_t)99);
    expect_fault(&ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, PAN);
}

static void test_fault_names(void) {
    NB_CHECK_EQ_STR("none", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_NONE));
    NB_CHECK_EQ_STR("config", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_CONFIG));
    NB_CHECK_EQ_STR("over_temp", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_OVER_TEMP));
    NB_CHECK_EQ_STR("brownout", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_BROWNOUT));
    NB_CHECK_EQ_STR("over_voltage", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_OVER_VOLTAGE));
    NB_CHECK_EQ_STR("stall_load", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_STALL_LOAD));
    NB_CHECK_EQ_STR("stall_tracking", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_STALL_TRACKING));
    NB_CHECK_EQ_STR("heartbeat", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_HEARTBEAT));
    NB_CHECK_EQ_STR("feedback_lost", nb_motion_safety_fault_name(NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST));
    NB_CHECK_EQ_STR("unknown", nb_motion_safety_fault_name((nb_motion_safety_fault_t)99));
}

int main(void) {
    NB_RUN(test_starts_in_init_and_cannot_move);
    NB_RUN(test_arm_needs_feedback_on_every_axis);
    NB_RUN(test_command_accepts_in_range_and_refuses_outside);
    NB_RUN(test_clamp_position);
    NB_RUN(test_over_temperature_faults);
    NB_RUN(test_brownout_faults);
    NB_RUN(test_unreadable_voltage_counts_as_brownout);
    NB_RUN(test_over_voltage_faults);
    NB_RUN(test_sustained_load_faults_after_stall_time);
    NB_RUN(test_load_spike_that_drops_does_not_fault);
    NB_RUN(test_load_timer_is_wrap_safe);
    NB_RUN(test_goal_not_reached_faults_after_timeout);
    NB_RUN(test_arriving_at_goal_clears_tracking_timer);
    NB_RUN(test_small_goal_steps_do_not_hide_a_stalled_servo);
    NB_RUN(test_large_goal_jump_restarts_tracking_window);
    NB_RUN(test_heartbeat_timeout_faults);
    NB_RUN(test_heartbeat_refreshes_the_deadline);
    NB_RUN(test_arm_counts_as_first_heartbeat);
    NB_RUN(test_heartbeat_timeout_across_clock_wraparound);
    NB_RUN(test_stale_now_does_not_fault_heartbeat);
    NB_RUN(test_command_catches_stale_heartbeat_without_tick);
    NB_RUN(test_heartbeat_not_enforced_before_arm);
    NB_RUN(test_feedback_lost_faults_per_axis_after_timeout);
    NB_RUN(test_feedback_refresh_keeps_axes_alive);
    NB_RUN(test_feedback_timeout_not_enforced_before_arm);
    NB_RUN(test_command_catches_stale_feedback_without_tick);
    NB_RUN(test_arm_refused_with_stale_feedback);
    NB_RUN(test_feedback_lost_reset_needs_fresh_feedback_on_every_axis);
    NB_RUN(test_feedback_timeout_across_clock_wraparound);
    NB_RUN(test_fault_is_sticky_and_refuses_everything);
    NB_RUN(test_first_fault_reason_is_kept);
    NB_RUN(test_fault_in_init_also_latches);
    NB_RUN(test_reset_refused_while_temperature_persists);
    NB_RUN(test_reset_refused_while_voltage_persists);
    NB_RUN(test_reset_refused_while_overloaded_or_not_tracking);
    NB_RUN(test_heartbeat_fault_reset_needs_live_heartbeat);
    NB_RUN(test_reset_is_only_valid_from_fault);
    NB_RUN(test_after_reset_rearm_needs_fresh_feedback_and_goals_are_dropped);
    NB_RUN(test_trip_forces_fault_and_is_reset_when_no_condition);
    NB_RUN(test_invalid_config_starts_faulted_and_cannot_be_reset);
    NB_RUN(test_null_and_invalid_inputs_are_safe);
    NB_RUN(test_fault_names);
    NB_TEST_MAIN_END();
}
