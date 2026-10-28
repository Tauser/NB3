#ifndef NB3_MOTION_SERVICE_H
#define NB3_MOTION_SERVICE_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_head_gesture.h"
#include "core/nb_motion_safety.h"
#include "core/nb_scs.h"
#include "motion/scs_bus.h"

// The head's servos (docs/ROADMAP.md stage 4). Its task is the only one that
// talks to the servo bus: every cycle it reads both servos, feeds
// motion_safety and beats its heart, and every write passes the safety's
// veto first.
//
// - At boot the torque is released: the head is free until a move is asked.
// - The first move arms the safety (fresh readings, both servos inside their
//   limits). Before the torque comes on, each servo's goal is set to where it
//   already is, so holding never starts with a jump; the same happens after
//   Relax().
// - Moves are plans of waypoints (core/nb_head_gesture) played one after the
//   other; a new plan replaces the one playing.
// - FAULT releases both servos at once (retrying until both are confirmed
//   free), shows a dizzy face for a moment and stays until Recover() is
//   asked and the safety agrees the cause is gone.
class MotionService {
public:
    MotionService(uint8_t pan_id, uint8_t tilt_id, ExpressionArbiter* arbiter);
    ~MotionService();

    MotionService(const MotionService&) = delete;
    MotionService& operator=(const MotionService&) = delete;

    // Where the head is, its limits and how it is mounted, for planning.
    nb_head_geometry_t Geometry() const { return geometry_; }

    // Any task. False when the head is stopped for safety or has no servos;
    // otherwise the motion task plays it from its next cycle.
    bool Play(const nb_head_plan_t& plan);
    // Brings the head back to center and then releases both servos (at once
    // when not holding); the next move holds again.
    void Relax();
    // One axis to `position` in `time_ms`, as a one-point plan (bench).
    void RequestGoal(nb_motion_safety_axis_t axis, uint16_t position, uint16_t time_ms);

    nb_motion_safety_state_t State();
    nb_motion_safety_fault_t Fault();

    struct Status {
        nb_motion_safety_state_t state = NB_MOTION_SAFETY_STATE_INIT;
        nb_motion_safety_fault_t fault = NB_MOTION_SAFETY_FAULT_NONE;
        nb_motion_safety_axis_t fault_axis = NB_MOTION_SAFETY_AXIS_PAN;
        bool holding = false;   // torque on
        bool moving = false;    // a plan is playing
        bool answering[NB_MOTION_SAFETY_AXIS_COUNT] = {};
        uint16_t position[NB_MOTION_SAFETY_AXIS_COUNT] = {};
        bool out_of_range[NB_MOTION_SAFETY_AXIS_COUNT] = {};  // no arming until put back
        uint8_t temperature_c[NB_MOTION_SAFETY_AXIS_COUNT] = {};
        uint16_t voltage_mv[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    };
    Status Read();

    enum class RecoverResult { kRecovered = 1, kRefused, kNotFaulted, kNoAnswer };
    // Explicit way out of FAULT, for the MCP tool. Waits up to ~0.5 s for the
    // motion task; refused while the cause is still there.
    RecoverResult Recover();

private:
    static void TaskEntry(void* arg);
    void Run();
    void FindServos();
    bool Release();
    bool Hold(uint32_t now_ms);
    void PlayStep(uint32_t now_ms);
    void HandleRecover(uint32_t now_ms);
    void PublishStatus();
    void LogReadings();

    static constexpr int kRecoverPending = 0;

    ScsBus bus_;
    ExpressionArbiter* arbiter_;
    uint8_t ids_[NB_MOTION_SAFETY_AXIS_COUNT];
    nb_head_geometry_t geometry_;
    std::mutex mutex_;  // pending_plan_, status_
    nb_head_plan_t pending_plan_ = {};
    bool plan_pending_ = false;
    Status status_;
    std::atomic<bool> relax_asked_{false};
    std::atomic<bool> recover_asked_{false};
    std::atomic<int> recover_result_{kRecoverPending};
    // The safety as other tasks see it, copied each cycle.
    std::atomic<int> state_{NB_MOTION_SAFETY_STATE_INIT};
    std::atomic<int> fault_{NB_MOTION_SAFETY_FAULT_NONE};
    // Motion task only.
    nb_motion_safety_t safety_;
    nb_head_plan_t plan_ = {};
    int plan_step_ = 0;      // next waypoint to send
    uint32_t step_due_ms_ = 0;
    bool torque_on_ = false;
    bool release_after_plan_ = false;  // a relax: center, then let go
    bool fault_logged_ = false;
    bool seen_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    nb_scs_feedback_t last_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint8_t last_error_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint32_t misses_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint32_t last_misses_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    // Range seen since boot: moving the servos by hand gives the real limits.
    uint16_t min_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint16_t max_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_MOTION_SERVICE_H
