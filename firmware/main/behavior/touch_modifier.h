#ifndef NB3_TOUCH_MODIFIER_H
#define NB3_TOUCH_MODIFIER_H

#include <driver/touch_sens.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_touch.h"

// Body touch. Its task is the only owner of the touch peripheral: it samples
// the pad, runs nb_touch and turns taps and petting into face proposals.
class TouchModifier {
public:
    TouchModifier(int channel, ExpressionArbiter& arbiter);
    ~TouchModifier();

    TouchModifier(const TouchModifier&) = delete;
    TouchModifier& operator=(const TouchModifier&) = delete;

private:
    static void TaskEntry(void* arg);
    void Run();
    void OnEvent(nb_touch_event_t event);

    ExpressionArbiter& arbiter_;
    touch_sensor_handle_t sensor_ = nullptr;
    touch_channel_handle_t channel_ = nullptr;
    TaskHandle_t task_ = nullptr;
    nb_touch_t touch_;
};

#endif  // NB3_TOUCH_MODIFIER_H
