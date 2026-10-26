#ifndef NB3_TOUCH_MODIFIER_H
#define NB3_TOUCH_MODIFIER_H

#include <driver/touch_sens.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "behavior/expression_arbiter.h"
#include "behavior/mood_modifier.h"
#include "core/nb_touch.h"

// Body touch. Its task is the only owner of the touch peripheral: it samples
// the pad, runs nb_touch and turns taps and petting into face proposals and
// mood stimuli (petting keeps warming the mood in pulses while it lasts).
class TouchModifier {
public:
    TouchModifier(int channel, ExpressionArbiter& arbiter, MoodModifier& mood);
    ~TouchModifier();

    TouchModifier(const TouchModifier&) = delete;
    TouchModifier& operator=(const TouchModifier&) = delete;

private:
    static void TaskEntry(void* arg);
    void Run();
    void OnEvent(nb_touch_event_t event);

    ExpressionArbiter& arbiter_;
    MoodModifier& mood_;
    uint32_t last_pet_pulse_ms_ = 0;
    touch_sensor_handle_t sensor_ = nullptr;
    touch_channel_handle_t channel_ = nullptr;
    TaskHandle_t task_ = nullptr;
    nb_touch_t touch_;
};

#endif  // NB3_TOUCH_MODIFIER_H
