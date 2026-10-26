#ifndef NB3_MOOD_LIGHTS_H
#define NB3_MOOD_LIGHTS_H

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_mood.h"

// Body LEDs that follow the face. Its task is the only owner of the strip:
// it reads the arbiter's winning expression and shows the nb_mood color.
class MoodLights {
public:
    MoodLights(gpio_num_t gpio, uint32_t count, uint8_t max_brightness,
               ExpressionArbiter& arbiter);
    ~MoodLights();

    MoodLights(const MoodLights&) = delete;
    MoodLights& operator=(const MoodLights&) = delete;

private:
    static void TaskEntry(void* arg);
    void Run();

    ExpressionArbiter& arbiter_;
    led_strip_handle_t strip_ = nullptr;
    uint32_t count_;
    TaskHandle_t task_ = nullptr;
    nb_mood_t mood_;
};

#endif  // NB3_MOOD_LIGHTS_H
