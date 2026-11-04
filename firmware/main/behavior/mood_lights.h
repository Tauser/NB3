#ifndef NB3_MOOD_LIGHTS_H
#define NB3_MOOD_LIGHTS_H

#include <driver/gpio.h>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_conv_led.h"
#include "core/nb_led_settings.h"
#include "core/nb_mood.h"

// Body LEDs that follow the face. Its task is the only owner of the strip:
// it reads the arbiter's winning expression and shows the nb_mood color,
// except during a conversation, when the conversation state takes over.
class MoodLights {
public:
    MoodLights(gpio_num_t gpio, uint32_t count, uint8_t max_brightness,
               ExpressionArbiter& arbiter);
    ~MoodLights();

    MoodLights(const MoodLights&) = delete;
    MoodLights& operator=(const MoodLights&) = delete;

    // Any task. The owner's choices (web panel): on or off, brightness within the
    // ceiling of nb_led_settings, a fixed color or the mood's. Kept across restarts.
    nb_led_settings_t Settings();
    void Apply(const nb_led_settings_t& settings);

private:
    static void TaskEntry(void* arg);
    void Run();

    ExpressionArbiter& arbiter_;
    led_strip_handle_t strip_ = nullptr;
    uint32_t count_;
    TaskHandle_t task_ = nullptr;
    nb_mood_t mood_;
    nb_conv_led_t conversation_;
    std::mutex settings_mutex_;
    nb_led_settings_t settings_;  // under settings_mutex_
};

#endif  // NB3_MOOD_LIGHTS_H
