#ifndef NB_MOTION_SAFETY_H
#define NB_MOTION_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hard veto under every servo write. The motion owner asks nb_motion_safety_command()
// before writing a goal position and feeds it the servo feedback and a heartbeat;
// nothing here touches hardware, reads a clock, or allocates. Time and sensor
// readings are always parameters, and all time comparisons are uint32 wrap-safe.
//
// States: INIT (not armed, no motion) -> OK (armed, motion allowed) -> FAULT.
// FAULT is sticky: it only leaves through nb_motion_safety_reset_fault(), which is
// refused while the condition that caused it is still observed.
//
// Position policy: an out-of-range goal is refused, never silently moved. Callers
// that prefer to saturate use nb_motion_safety_clamp_position() first and then ask
// for the clamped value.

typedef enum {
    NB_MOTION_SAFETY_STATE_INIT = 0,
    NB_MOTION_SAFETY_STATE_OK,
    NB_MOTION_SAFETY_STATE_FAULT
} nb_motion_safety_state_t;

typedef enum {
    NB_MOTION_SAFETY_FAULT_NONE = 0,
    NB_MOTION_SAFETY_FAULT_CONFIG,         // invalid configuration at init
    NB_MOTION_SAFETY_FAULT_OVER_TEMP,      // servo temperature at/above the limit
    NB_MOTION_SAFETY_FAULT_BROWNOUT,       // bus voltage below the floor (or unreadable)
    NB_MOTION_SAFETY_FAULT_OVER_VOLTAGE,   // bus voltage above the ceiling
    NB_MOTION_SAFETY_FAULT_STALL_LOAD,     // load at/above critical for too long
    NB_MOTION_SAFETY_FAULT_STALL_TRACKING, // position not reaching the goal in time
    NB_MOTION_SAFETY_FAULT_HEARTBEAT,      // motion owner stopped feeding the heartbeat
    NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST   // an axis stopped reporting feedback while armed
} nb_motion_safety_fault_t;

typedef enum {
    NB_MOTION_SAFETY_AXIS_PAN = 0,
    NB_MOTION_SAFETY_AXIS_TILT,
    NB_MOTION_SAFETY_AXIS_COUNT
} nb_motion_safety_axis_t;

typedef enum {
    NB_MOTION_SAFETY_CMD_OK = 0,
    NB_MOTION_SAFETY_CMD_REFUSED_STATE,   // not armed, or faulted
    NB_MOTION_SAFETY_CMD_REFUSED_RANGE,   // outside the axis limits
    NB_MOTION_SAFETY_CMD_REFUSED_INVALID  // NULL instance or bad axis
} nb_motion_safety_cmd_result_t;

typedef struct {
    uint16_t pos_min[NB_MOTION_SAFETY_AXIS_COUNT];  // servo position units, inclusive
    uint16_t pos_max[NB_MOTION_SAFETY_AXIS_COUNT];  // servo position units, inclusive

    uint16_t load_critical;    // servo load magnitude units (0-1000)
    uint32_t stall_time_ms;    // load must stay >= load_critical this long

    uint16_t track_tolerance;  // |position - goal| within this counts as arrived
    uint32_t track_timeout_ms; // error above tolerance for this long = stalled
    // Away from the goal, the position must move more than track_tolerance
    // within this long. New goals do not restart it, so a stream of jumping
    // goals cannot keep a servo that never moves looking fine.
    uint32_t progress_timeout_ms;

    uint8_t temp_critical_c;   // fault at or above
    uint16_t brownout_mv;      // fault below
    uint16_t overvoltage_mv;   // fault above

    uint32_t heartbeat_timeout_ms;  // armed and no heartbeat for this long = fault
    uint32_t feedback_timeout_ms;   // armed and an axis silent for this long = fault
} nb_motion_safety_cfg_t;

// One servo's bus readings. load is the magnitude (direction bit already
// stripped). voltage_mv == 0 means "unreadable" and is treated as a brownout.
typedef struct {
    uint16_t position;
    uint16_t load;
    uint8_t temp_c;
    uint16_t voltage_mv;
} nb_motion_safety_feedback_t;

typedef struct {
    nb_motion_safety_cfg_t cfg;
    nb_motion_safety_state_t state;
    nb_motion_safety_fault_t fault;
    nb_motion_safety_axis_t fault_axis;

    uint32_t last_heartbeat_ms;
    bool heartbeat_seen;

    // Latest observed condition per axis. They feed the reset check, so they are
    // refreshed by every feedback sample whatever the state is.
    bool feedback_seen[NB_MOTION_SAFETY_AXIS_COUNT];
    uint32_t last_feedback_ms[NB_MOTION_SAFETY_AXIS_COUNT];
    bool cond_temp[NB_MOTION_SAFETY_AXIS_COUNT];
    bool cond_volt[NB_MOTION_SAFETY_AXIS_COUNT];
    bool cond_stall[NB_MOTION_SAFETY_AXIS_COUNT];
    bool cond_track[NB_MOTION_SAFETY_AXIS_COUNT];

    bool load_timer_on[NB_MOTION_SAFETY_AXIS_COUNT];
    uint32_t load_since_ms[NB_MOTION_SAFETY_AXIS_COUNT];
    bool goal_valid[NB_MOTION_SAFETY_AXIS_COUNT];
    uint16_t goal[NB_MOTION_SAFETY_AXIS_COUNT];
    bool track_timer_on[NB_MOTION_SAFETY_AXIS_COUNT];
    uint32_t track_since_ms[NB_MOTION_SAFETY_AXIS_COUNT];
    bool progress_on[NB_MOTION_SAFETY_AXIS_COUNT];
    uint16_t progress_ref[NB_MOTION_SAFETY_AXIS_COUNT];  // position at the last real move
    uint32_t progress_since_ms[NB_MOTION_SAFETY_AXIS_COUNT];
} nb_motion_safety_t;

// Defaults for the pan/tilt pair on SCS0009 servos (position 0-1023, load 0-1000).
nb_motion_safety_cfg_t nb_motion_safety_default_cfg(void);

// Starts in INIT. An invalid configuration (or NULL cfg) starts in FAULT with
// reason CONFIG, so a bad setup can never allow motion. Returns true when the
// configuration was accepted.
bool nb_motion_safety_init(nb_motion_safety_t *ms, const nb_motion_safety_cfg_t *cfg);

// INIT -> OK. Refused in FAULT, and until feedback has been seen for every axis
// (arming blind would defeat the sensor checks) or while that feedback is already
// older than the feedback timeout. Counts as the first heartbeat.
bool nb_motion_safety_arm(nb_motion_safety_t *ms, uint32_t now_ms);

// Feeds one servo's readings. Checks temperature, voltage, sustained load and
// goal tracking; may enter FAULT in any state. Returns false when the instance
// is faulted (after this call) or the arguments are invalid.
bool nb_motion_safety_update(nb_motion_safety_t *ms, nb_motion_safety_axis_t axis,
                             const nb_motion_safety_feedback_t *fb, uint32_t now_ms);

// Motion owner liveness. Cheap; call it every motion tick.
void nb_motion_safety_heartbeat(nb_motion_safety_t *ms, uint32_t now_ms);

// Periodic watchdog (heartbeat and per-axis feedback timeouts). Only enforced
// while armed: a servo that stops answering is a hazard the sensor checks cannot
// see on their own.
void nb_motion_safety_tick(nb_motion_safety_t *ms, uint32_t now_ms);

// The veto. Must return OK before every goal-position write; an accepted goal is
// remembered for the tracking check.
nb_motion_safety_cmd_result_t nb_motion_safety_command(nb_motion_safety_t *ms,
                                                       nb_motion_safety_axis_t axis,
                                                       uint16_t position, uint32_t now_ms);

// Saturates a position into the axis limits. Returns the input for a bad axis or
// NULL instance, which command() then refuses.
uint16_t nb_motion_safety_clamp_position(const nb_motion_safety_t *ms,
                                         nb_motion_safety_axis_t axis, uint16_t position);

// Forces FAULT from outside (e.g. a bus error the shell cannot express as
// feedback). Keeps the first recorded reason if already faulted.
void nb_motion_safety_trip(nb_motion_safety_t *ms, nb_motion_safety_fault_t reason,
                           nb_motion_safety_axis_t axis);

// Leaves FAULT for INIT. Refused (false) when not faulted, or while any fault
// condition is still observed: hot, out-of-range voltage, overloaded or
// non-tracking feedback, a stale heartbeat or stale feedback on any axis when
// that was the reason, or a configuration fault. A reset also demands fresh feedback before the next arm.
bool nb_motion_safety_reset_fault(nb_motion_safety_t *ms, uint32_t now_ms);

nb_motion_safety_state_t nb_motion_safety_get_state(const nb_motion_safety_t *ms);
nb_motion_safety_fault_t nb_motion_safety_get_fault(const nb_motion_safety_t *ms);
nb_motion_safety_axis_t nb_motion_safety_get_fault_axis(const nb_motion_safety_t *ms);
bool nb_motion_safety_can_move(const nb_motion_safety_t *ms);
const char *nb_motion_safety_fault_name(nb_motion_safety_fault_t fault);

#ifdef __cplusplus
}
#endif

#endif // NB_MOTION_SAFETY_H
