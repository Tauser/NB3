#ifndef NB3_STATE_LED_H
#define NB3_STATE_LED_H

#include "behavior/expression_arbiter.h"
#include "behavior/mood_modifier.h"
#include "led/led.h"

// xiaozhi reports every device-state change through Led::OnStateChanged().
// This Led keeps driving the real status LED and also turns the state into
// face behavior: IDLE resets transient expressions, listening shows attention.
// Being listened to and faults also reach the mood.
class StateLed : public Led {
public:
    StateLed(Led& status_led, ExpressionArbiter& arbiter, MoodModifier& mood)
        : status_led_(status_led), arbiter_(arbiter), mood_(mood) {}

    void OnStateChanged() override;

private:
    Led& status_led_;
    ExpressionArbiter& arbiter_;
    MoodModifier& mood_;
};

#endif  // NB3_STATE_LED_H
