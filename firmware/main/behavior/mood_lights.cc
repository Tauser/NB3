#include "mood_lights.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "MoodLights"

namespace {

constexpr uint32_t kFramePeriodMs = 30;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

MoodLights::MoodLights(gpio_num_t gpio, uint32_t count, uint8_t max_brightness,
                       ExpressionArbiter& arbiter)
    : arbiter_(arbiter), count_(count) {
    nb_mood_init(&mood_, max_brightness, NowMs());

    led_strip_config_t strip_cfg = {};
    strip_cfg.strip_gpio_num = gpio;
    strip_cfg.max_leds = count;
    strip_cfg.led_model = LED_MODEL_WS2812;
    strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    led_strip_rmt_config_t rmt_cfg = {};
    rmt_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_cfg.resolution_hz = 10 * 1000 * 1000;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip_));
    led_strip_clear(strip_);

    if (xTaskCreate(TaskEntry, "nb_mood", 3072, this, 3, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task not created: the body lights stay off");
    }
}

MoodLights::~MoodLights() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    if (strip_ != nullptr) {
        led_strip_clear(strip_);
        led_strip_del(strip_);
    }
}

void MoodLights::TaskEntry(void* arg) {
    static_cast<MoodLights*>(arg)->Run();
}

void MoodLights::Run() {
    while (true) {
        nb_mood_set_expression(&mood_, arbiter_.Resolve());
        nb_rgb_t color = nb_mood_step(&mood_, NowMs());
        for (uint32_t i = 0; i < count_; i++) {
            led_strip_set_pixel(strip_, i, color.r, color.g, color.b);
        }
        if (led_strip_refresh(strip_) != ESP_OK) {
            ESP_LOGW(TAG, "refresh failed");
        }
        vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
    }
}
