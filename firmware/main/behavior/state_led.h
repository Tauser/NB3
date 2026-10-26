#ifndef NB3_STATE_LED_H
#define NB3_STATE_LED_H

#include "behavior/expression_arbiter.h"
#include "behavior/mood_modifier.h"
#include "device_state.h"
#include "led/led.h"

// xiaozhi reports every device-state change through Led::OnStateChanged().
// This Led keeps driving the real status LED and also turns the state into
// face behavior: IDLE resets transient expressions, listening shows attention
// and, once the person has spoken and gone quiet, thinking until the reply.
// Entering listening and faults also reach the mood. While listening, xiaozhi
// reports the state again on every voice-activity change, so only a change
// of state counts for the mood.
class StateLed : public Led {
public:
    StateLed(Led& status_led, ExpressionArbiter& arbiter, MoodModifier& mood)
        : status_led_(status_led), arbiter_(arbiter), mood_(mood) {}

    void OnStateChanged() override;

private:
    Led& status_led_;
    ExpressionArbiter& arbiter_;
    MoodModifier& mood_;
    DeviceState last_state_ = kDeviceStateUnknown;
    bool heard_voice_ = false;  // the person spoke in this listening turn
};

#endif  // NB3_STATE_LED_H
