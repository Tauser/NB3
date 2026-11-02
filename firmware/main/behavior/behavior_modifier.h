#ifndef NB3_BEHAVIOR_MODIFIER_H
#define NB3_BEHAVIOR_MODIFIER_H

#include <functional>
#include <mutex>

#include <esp_timer.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_behavior.h"
#include "device_state.h"

// The robot's behavioral state (core/nb_behavior): idle, engaged in a
// conversation, reacting to a touch, sleeping or in fault. It reads
// xiaozhi's device state without duplicating the conversation, and it is the
// one place that tells the arbiter the robot is back at rest, which clears
// every transient proposal and leaves the mood alone.
class BehaviorModifier {
public:
    explicit BehaviorModifier(ExpressionArbiter& arbiter);
    ~BehaviorModifier();

    BehaviorModifier(const BehaviorModifier&) = delete;
    BehaviorModifier& operator=(const BehaviorModifier&) = delete;

    void OnDevice(DeviceState state);
    void OnTouch(bool touching);
    void Sleep();
    void Wake();
    void Recover();
    void SetQuiet(bool quiet);
    nb_behavior_state_t State();

    // Told on every entry to and exit from SLEEPING, from whichever task caused
    // the transition and with the behavior lock held: it must only store the
    // value (the face keeps it in an atomic). Set once, before the first event.
    void SetSleepSink(std::function<void(bool)> sink) { sleep_sink_ = std::move(sink); }

private:
    static void OnTimer(void* arg);
    void Apply(const nb_behavior_transition_t& transition);

    ExpressionArbiter& arbiter_;
    std::mutex mutex_;
    nb_behavior_t behavior_;
    esp_timer_handle_t timer_ = nullptr;
    std::function<void(bool)> sleep_sink_;
};

#endif  // NB3_BEHAVIOR_MODIFIER_H
