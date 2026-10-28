#ifndef NB3_MOTION_SERVICE_H
#define NB3_MOTION_SERVICE_H

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/nb_motion_safety.h"
#include "core/nb_scs.h"
#include "motion/scs_bus.h"

// The head's servos (docs/ROADMAP.md stage 4). Its task is the only one that
// talks to the servo bus: every cycle it reads both servos, feeds
// motion_safety and beats its heart. For now the safety is never armed: the
// only write is releasing the torque at boot (always allowed by the safety),
// so the head is free while the bus, the readings and the real limits are
// checked on the bench.
class MotionService {
public:
    MotionService(uint8_t pan_id, uint8_t tilt_id);
    ~MotionService();

    MotionService(const MotionService&) = delete;
    MotionService& operator=(const MotionService&) = delete;

private:
    static void TaskEntry(void* arg);
    void Run();
    void FindServos();
    void Relax();
    void LogReadings();

    ScsBus bus_;
    uint8_t ids_[NB_MOTION_SAFETY_AXIS_COUNT];
    nb_motion_safety_t safety_;
    // Motion task only.
    bool seen_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    nb_scs_feedback_t last_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint8_t last_error_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint32_t misses_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    // Range seen since boot: moving the head by hand gives the real limits.
    uint16_t min_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    uint16_t max_[NB_MOTION_SAFETY_AXIS_COUNT] = {};
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_MOTION_SERVICE_H
