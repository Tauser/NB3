#include "nb_motion_safety.h"

#include <stddef.h>
#include <string.h>

static bool axis_valid(nb_motion_safety_axis_t axis) {
    return (int)axis >= 0 && axis < NB_MOTION_SAFETY_AXIS_COUNT;
}

// Wrap-safe elapsed time. A now_ms that is behind the reference (a stale sample
// taken before a slower read) counts as zero instead of a huge gap.
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t since_ms) {
    int32_t delta = (int32_t)(now_ms - since_ms);
    return delta > 0 ? (uint32_t)delta : 0u;
}

static uint16_t abs_diff(uint16_t a, uint16_t b) {
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static bool feedback_stale(const nb_motion_safety_t *ms, int axis, uint32_t now_ms) {
    return !ms->feedback_seen[axis] ||
           elapsed_ms(now_ms, ms->last_feedback_ms[axis]) >= ms->cfg.feedback_timeout_ms;
}

static void enter_fault(nb_motion_safety_t *ms, nb_motion_safety_fault_t reason,
                        nb_motion_safety_axis_t axis) {
    if (ms->state == NB_MOTION_SAFETY_STATE_FAULT) {
        return;  // the first cause stays on record
    }
    ms->state = NB_MOTION_SAFETY_STATE_FAULT;
    ms->fault = reason;
    ms->fault_axis = axis;
}

static bool cfg_valid(const nb_motion_safety_cfg_t *cfg) {
    if (cfg == NULL) {
        return false;
    }
    for (int i = 0; i < NB_MOTION_SAFETY_AXIS_COUNT; i++) {
        if (cfg->pos_min[i] > cfg->pos_max[i]) {
            return false;
        }
    }
    // Durations must stay under half the uint32 range for the wrap-safe compare.
    return cfg->load_critical > 0 && cfg->stall_time_ms < 0x80000000u &&
           cfg->track_timeout_ms > 0 && cfg->track_timeout_ms < 0x80000000u &&
           cfg->temp_critical_c > 0 && cfg->brownout_mv > 0 &&
           cfg->overvoltage_mv > cfg->brownout_mv && cfg->heartbeat_timeout_ms > 0 &&
           cfg->heartbeat_timeout_ms < 0x80000000u && cfg->feedback_timeout_ms > 0 &&
           cfg->feedback_timeout_ms < 0x80000000u;
}

nb_motion_safety_cfg_t nb_motion_safety_default_cfg(void) {
    nb_motion_safety_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pos_min[NB_MOTION_SAFETY_AXIS_PAN] = 410;
    cfg.pos_max[NB_MOTION_SAFETY_AXIS_PAN] = 614;
    cfg.pos_min[NB_MOTION_SAFETY_AXIS_TILT] = 461;
    cfg.pos_max[NB_MOTION_SAFETY_AXIS_TILT] = 563;
    cfg.load_critical = 700;
    cfg.stall_time_ms = 100;
    cfg.track_tolerance = 20;
    cfg.track_timeout_ms = 1500;
    cfg.temp_critical_c = 70;
    cfg.brownout_mv = 4500;
    cfg.overvoltage_mv = 6500;
    cfg.heartbeat_timeout_ms = 600;
    cfg.feedback_timeout_ms = 300;
    return cfg;
}

bool nb_motion_safety_init(nb_motion_safety_t *ms, const nb_motion_safety_cfg_t *cfg) {
    if (ms == NULL) {
        return false;
    }
    memset(ms, 0, sizeof(*ms));
    if (!cfg_valid(cfg)) {
        ms->state = NB_MOTION_SAFETY_STATE_FAULT;
        ms->fault = NB_MOTION_SAFETY_FAULT_CONFIG;
        return false;
    }
    ms->cfg = *cfg;
    ms->state = NB_MOTION_SAFETY_STATE_INIT;
    return true;
}

bool nb_motion_safety_arm(nb_motion_safety_t *ms, uint32_t now_ms) {
    if (ms == NULL || ms->state != NB_MOTION_SAFETY_STATE_INIT) {
        return false;
    }
    for (int i = 0; i < NB_MOTION_SAFETY_AXIS_COUNT; i++) {
        if (feedback_stale(ms, i, now_ms)) {
            return false;
        }
    }
    ms->last_heartbeat_ms = now_ms;
    ms->heartbeat_seen = true;
    ms->state = NB_MOTION_SAFETY_STATE_OK;
    return true;
}

bool nb_motion_safety_update(nb_motion_safety_t *ms, nb_motion_safety_axis_t axis,
                             const nb_motion_safety_feedback_t *fb, uint32_t now_ms) {
    if (ms == NULL || fb == NULL || !axis_valid(axis)) {
        return false;
    }
    const nb_motion_safety_cfg_t *cfg = &ms->cfg;
    int a = (int)axis;
    ms->feedback_seen[a] = true;
    ms->last_feedback_ms[a] = now_ms;

    ms->cond_temp[a] = fb->temp_c >= cfg->temp_critical_c;
    bool low = fb->voltage_mv < cfg->brownout_mv;
    bool high = fb->voltage_mv > cfg->overvoltage_mv;
    ms->cond_volt[a] = low || high;

    if (fb->load >= cfg->load_critical) {
        if (!ms->load_timer_on[a]) {
            ms->load_timer_on[a] = true;
            ms->load_since_ms[a] = now_ms;
        }
        ms->cond_stall[a] = elapsed_ms(now_ms, ms->load_since_ms[a]) >= cfg->stall_time_ms;
    } else {
        ms->load_timer_on[a] = false;
        ms->cond_stall[a] = false;
    }

    // Tracking error is only meaningful once a goal has been accepted.
    ms->cond_track[a] = false;
    if (ms->goal_valid[a] && abs_diff(fb->position, ms->goal[a]) > cfg->track_tolerance) {
        if (!ms->track_timer_on[a]) {
            ms->track_timer_on[a] = true;
            ms->track_since_ms[a] = now_ms;
        }
        ms->cond_track[a] = elapsed_ms(now_ms, ms->track_since_ms[a]) >= cfg->track_timeout_ms;
    } else {
        ms->track_timer_on[a] = false;
    }

    if (ms->cond_temp[a]) {
        enter_fault(ms, NB_MOTION_SAFETY_FAULT_OVER_TEMP, axis);
    } else if (low) {
        enter_fault(ms, NB_MOTION_SAFETY_FAULT_BROWNOUT, axis);
    } else if (high) {
        enter_fault(ms, NB_MOTION_SAFETY_FAULT_OVER_VOLTAGE, axis);
    } else if (ms->cond_stall[a]) {
        enter_fault(ms, NB_MOTION_SAFETY_FAULT_STALL_LOAD, axis);
    } else if (ms->cond_track[a]) {
        enter_fault(ms, NB_MOTION_SAFETY_FAULT_STALL_TRACKING, axis);
    }
    return ms->state != NB_MOTION_SAFETY_STATE_FAULT;
}

void nb_motion_safety_heartbeat(nb_motion_safety_t *ms, uint32_t now_ms) {
    if (ms == NULL) {
        return;
    }
    ms->last_heartbeat_ms = now_ms;
    ms->heartbeat_seen = true;
}

void nb_motion_safety_tick(nb_motion_safety_t *ms, uint32_t now_ms) {
    if (ms == NULL || ms->state != NB_MOTION_SAFETY_STATE_OK) {
        return;
    }
    if (!ms->heartbeat_seen ||
        elapsed_ms(now_ms, ms->last_heartbeat_ms) >= ms->cfg.heartbeat_timeout_ms) {
        // The heartbeat belongs to the whole motion owner, not to one servo.
        enter_fault(ms, NB_MOTION_SAFETY_FAULT_HEARTBEAT, NB_MOTION_SAFETY_AXIS_PAN);
        return;
    }
    for (int i = 0; i < NB_MOTION_SAFETY_AXIS_COUNT; i++) {
        if (feedback_stale(ms, i, now_ms)) {
            enter_fault(ms, NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, (nb_motion_safety_axis_t)i);
            return;
        }
    }
}

uint16_t nb_motion_safety_clamp_position(const nb_motion_safety_t *ms,
                                         nb_motion_safety_axis_t axis, uint16_t position) {
    if (ms == NULL || !axis_valid(axis)) {
        return position;
    }
    if (position < ms->cfg.pos_min[axis]) {
        return ms->cfg.pos_min[axis];
    }
    if (position > ms->cfg.pos_max[axis]) {
        return ms->cfg.pos_max[axis];
    }
    return position;
}

nb_motion_safety_cmd_result_t nb_motion_safety_command(nb_motion_safety_t *ms,
                                                       nb_motion_safety_axis_t axis,
                                                       uint16_t position, uint32_t now_ms) {
    if (ms == NULL || !axis_valid(axis)) {
        return NB_MOTION_SAFETY_CMD_REFUSED_INVALID;
    }
    // Stale heartbeat or feedback is caught here too, so the veto never depends
    // on the watchdog tick having run since the owner or a servo went silent.
    nb_motion_safety_tick(ms, now_ms);
    if (ms->state != NB_MOTION_SAFETY_STATE_OK) {
        return NB_MOTION_SAFETY_CMD_REFUSED_STATE;
    }
    if (position < ms->cfg.pos_min[axis] || position > ms->cfg.pos_max[axis]) {
        return NB_MOTION_SAFETY_CMD_REFUSED_RANGE;
    }
    int a = (int)axis;
    // A goal near the previous one keeps the running tracking timer, so a slowly
    // streamed target cannot hide a servo that never arrives; a real jump
    // legitimately needs a fresh travel window.
    if (!ms->goal_valid[a] || abs_diff(position, ms->goal[a]) > ms->cfg.track_tolerance) {
        ms->track_timer_on[a] = false;
    }
    ms->goal[a] = position;
    ms->goal_valid[a] = true;
    return NB_MOTION_SAFETY_CMD_OK;
}

void nb_motion_safety_trip(nb_motion_safety_t *ms, nb_motion_safety_fault_t reason,
                           nb_motion_safety_axis_t axis) {
    if (ms == NULL || reason == NB_MOTION_SAFETY_FAULT_NONE) {
        return;
    }
    enter_fault(ms, reason, axis_valid(axis) ? axis : NB_MOTION_SAFETY_AXIS_PAN);
}

bool nb_motion_safety_reset_fault(nb_motion_safety_t *ms, uint32_t now_ms) {
    if (ms == NULL || ms->state != NB_MOTION_SAFETY_STATE_FAULT ||
        ms->fault == NB_MOTION_SAFETY_FAULT_CONFIG) {
        return false;  // a bad configuration needs a new init, not a reset
    }
    for (int i = 0; i < NB_MOTION_SAFETY_AXIS_COUNT; i++) {
        if (ms->cond_temp[i] || ms->cond_volt[i] || ms->cond_stall[i] || ms->cond_track[i]) {
            return false;
        }
    }
    if (ms->fault == NB_MOTION_SAFETY_FAULT_HEARTBEAT &&
        (!ms->heartbeat_seen ||
         elapsed_ms(now_ms, ms->last_heartbeat_ms) >= ms->cfg.heartbeat_timeout_ms)) {
        return false;
    }
    if (ms->fault == NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST) {
        for (int i = 0; i < NB_MOTION_SAFETY_AXIS_COUNT; i++) {
            if (feedback_stale(ms, i, now_ms)) {
                return false;
            }
        }
    }
    ms->state = NB_MOTION_SAFETY_STATE_INIT;
    ms->fault = NB_MOTION_SAFETY_FAULT_NONE;
    ms->fault_axis = NB_MOTION_SAFETY_AXIS_PAN;
    ms->heartbeat_seen = false;
    // Old goals and timers describe a motion that no longer exists, and the
    // next arm must rest on fresh feedback.
    memset(ms->feedback_seen, 0, sizeof(ms->feedback_seen));
    memset(ms->load_timer_on, 0, sizeof(ms->load_timer_on));
    memset(ms->goal_valid, 0, sizeof(ms->goal_valid));
    memset(ms->track_timer_on, 0, sizeof(ms->track_timer_on));
    return true;
}

nb_motion_safety_state_t nb_motion_safety_get_state(const nb_motion_safety_t *ms) {
    return ms != NULL ? ms->state : NB_MOTION_SAFETY_STATE_FAULT;
}

nb_motion_safety_fault_t nb_motion_safety_get_fault(const nb_motion_safety_t *ms) {
    return ms != NULL ? ms->fault : NB_MOTION_SAFETY_FAULT_CONFIG;
}

nb_motion_safety_axis_t nb_motion_safety_get_fault_axis(const nb_motion_safety_t *ms) {
    return ms != NULL ? ms->fault_axis : NB_MOTION_SAFETY_AXIS_PAN;
}

bool nb_motion_safety_can_move(const nb_motion_safety_t *ms) {
    return ms != NULL && ms->state == NB_MOTION_SAFETY_STATE_OK;
}

const char *nb_motion_safety_fault_name(nb_motion_safety_fault_t fault) {
    switch (fault) {
    case NB_MOTION_SAFETY_FAULT_NONE: return "none";
    case NB_MOTION_SAFETY_FAULT_CONFIG: return "config";
    case NB_MOTION_SAFETY_FAULT_OVER_TEMP: return "over_temp";
    case NB_MOTION_SAFETY_FAULT_BROWNOUT: return "brownout";
    case NB_MOTION_SAFETY_FAULT_OVER_VOLTAGE: return "over_voltage";
    case NB_MOTION_SAFETY_FAULT_STALL_LOAD: return "stall_load";
    case NB_MOTION_SAFETY_FAULT_STALL_TRACKING: return "stall_tracking";
    case NB_MOTION_SAFETY_FAULT_HEARTBEAT: return "heartbeat";
    case NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST: return "feedback_lost";
    }
    return "unknown";
}
