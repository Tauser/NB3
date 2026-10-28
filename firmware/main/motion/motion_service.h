#ifndef NB3_MOTION_SERVICE_H
#define NB3_MOTION_SERVICE_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/nb_motion_safety.h"
#include "core/nb_scs.h"
#include "motion/scs_bus.h"

// The head's servos (docs/ROADMAP.md stage 4). Its task is the only one that
// talks to the servo bus: every cycle it reads both servos, feeds
// motion_safety and beats its heart, and every write passes the safety's
// veto first.
//
// - At boot the torque is released: the head is free until a goal is asked.
// - The first goal arms the safety (fresh readings needed), preloads each
//   servo's goal with where it already is, so nothing jumps, and only then
//   turns the torque on.
// - FAULT releases both servos at once and stays until a reboot.
class MotionService {
public:
    MotionService(uint8_t pan_id, uint8_t tilt_id);
    ~MotionService();

    MotionService(const MotionService&) = delete;
    MotionService& operator=(const MotionService&) = delete;

    // Asks for an axis to go to `position` in `time_ms`. Any task; the motion
    // task applies it on its next cycle, if the safety accepts it.
    void RequestGoal(nb_motion_safety_axis_t axis, uint16_t position, uint16_t time_ms);

    nb_motion_safety_state_t State();
    nb_motion_safety_fault_t Fault();

private:
    struct Goal {
        bool pending = false;
        uint16_t position = 0;
        uint16_t time_ms = 0;
    };

    static void TaskEntry(void* arg);
    void Run();
    void FindServos();
    void Relax();
    bool Arm(uint32_t now_ms);
    void ApplyGoals(uint32_t now_ms);
    void LogReadings();

    ScsBus bus_;
    uint8_t ids_[NB_MOTION_SAFETY_AXIS_COUNT];
    std::mutex mutex_;  // goals_
    Goal goals_[NB_MOTION_SAFETY_AXIS_COUNT];
    // The safety as other tasks see it, copied each cycle.
    std::atomic<int> state_{NB_MOTION_SAFETY_STATE_INIT};
    std::atomic<int> fault_{NB_MOTION_SAFETY_FAULT_NONE};
    nb_motion_safety_t safety_;  // motion task only
    // Motion task only.
    bool torque_on_ = false;
    bool fault_logged_ = false;
    bool seen_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    nb_scs_feedback_t last_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint8_t last_error_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint32_t misses_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    // Range seen since boot: moving the servos by hand gives the real limits.
    uint16_t min_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint16_t max_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_MOTION_SERVICE_H
