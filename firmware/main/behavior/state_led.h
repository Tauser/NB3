#ifndef NB3_STATE_LED_H
#define NB3_STATE_LED_H

#include "behavior/expression_arbiter.h"
#include "led/led.h"

// xiaozhi reports every device-state change through Led::OnStateChanged().
// This Led keeps driving the real status LED and also turns the state into
// face behavior: IDLE resets transient expressions, listening shows attention.
class StateLed : public Led {
public:
    StateLed(Led& status_led, ExpressionArbiter& arbiter)
        : status_led_(status_led), arbiter_(arbiter) {}

    void OnStateChanged() override;

private:
    Led& status_led_;
    ExpressionArbiter& arbiter_;
};

#endif  // NB3_STATE_LED_H
