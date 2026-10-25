#ifndef NB3_STATE_LED_H
#define NB3_STATE_LED_H

#include "behavior/expression_arbiter.h"
#include "face/face_display.h"
#include "led/led.h"

// xiaozhi reports every device-state change through Led::OnStateChanged().
// This Led keeps driving the real status LED and also turns the state into
// face behavior: IDLE resets transient expressions, speaking moves the mouth.
class StateLed : public Led {
public:
    StateLed(Led& status_led, ExpressionArbiter& arbiter, FaceDisplay& face)
        : status_led_(status_led), arbiter_(arbiter), face_(face) {}

    void OnStateChanged() override;

private:
    Led& status_led_;
    ExpressionArbiter& arbiter_;
    FaceDisplay& face_;
};

#endif  // NB3_STATE_LED_H
