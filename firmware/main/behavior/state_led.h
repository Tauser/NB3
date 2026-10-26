#ifndef NB3_STATE_LED_H
#define NB3_STATE_LED_H

#include <atomic>

#include <esp_timer.h>

#include "behavior/behavior_modifier.h"
#include "behavior/expression_arbiter.h"
#include "behavior/mood_modifier.h"
#include "device_state.h"
#include "led/led.h"

// xiaozhi reports every device-state change through Led::OnStateChanged().
// This Led keeps driving the real status LED, feeds the state to the
// behavioral state (which decides when the robot is back at rest), and turns
// it into face behavior: listening shows attention and, once the person has
// spoken and gone quiet, thinking until the reply.
// Thinking only shows if the wait lasts: a quick connect or a quick reply
// goes straight on, instead of flashing three faces in a second.
// Entering listening and faults also reach the mood. While listening, xiaozhi
// reports the state again on every voice-activity change, so only a change
// of state counts for the mood.
class StateLed : public Led {
public:
    StateLed(Led& status_led, ExpressionArbiter& arbiter, MoodModifier& mood,
             BehaviorModifier& behavior);
    ~StateLed();

    StateLed(const StateLed&) = delete;
    StateLed& operator=(const StateLed&) = delete;

    void OnStateChanged() override;

private:
    static void OnThinkTimer(void* arg);
    bool StillWaiting() const;

    Led& status_led_;
    ExpressionArbiter& arbiter_;
    MoodModifier& mood_;
    BehaviorModifier& behavior_;
    DeviceState last_state_ = kDeviceStateUnknown;
    std::atomic<bool> heard_voice_{false};  // the person spoke in this listening turn
    esp_timer_handle_t think_timer_ = nullptr;
};

#endif  // NB3_STATE_LED_H
