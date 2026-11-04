#include "mood_lights.h"

#include <esp_log.h>
#include <esp_timer.h>

#include "application.h"
#include "settings.h"

#define TAG "MoodLights"

namespace {

constexpr uint32_t kFramePeriodMs = 30;

constexpr const char* kNamespace = "nb3_led";

void Save(const nb_led_settings_t& s) {
    Settings store(kNamespace, true);
    store.SetBool("on", s.on);
    store.SetInt("bright", s.brightness);
    store.SetBool("fixed", s.fixed_color);
    store.SetInt("rgb", (s.color.r << 16) | (s.color.g << 8) | s.color.b);
}

nb_led_settings_t Load(uint8_t default_brightness) {
    nb_led_settings_t s;
    nb_led_settings_default(&s, default_brightness);
    Settings store(kNamespace, false);
    s.on = store.GetBool("on", s.on);
    s.brightness = static_cast<uint8_t>(store.GetInt("bright", s.brightness));
    s.fixed_color = store.GetBool("fixed", s.fixed_color);
    const int32_t rgb = store.GetInt("rgb", 0xFFFFFF);
    s.color.r = static_cast<uint8_t>(rgb >> 16);
    s.color.g = static_cast<uint8_t>(rgb >> 8);
    s.color.b = static_cast<uint8_t>(rgb);
    nb_led_settings_clamp(&s);
    return s;
}

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

nb_conv_state_t ConversationOf(DeviceState state) {
    switch (state) {
    case kDeviceStateConnecting: return NB_CONV_WAITING;
    case kDeviceStateListening: return NB_CONV_LISTENING;
    case kDeviceStateSpeaking: return NB_CONV_SPEAKING;
    default: return NB_CONV_REST;
    }
}

}  // namespace

MoodLights::MoodLights(gpio_num_t gpio, uint32_t count, uint8_t max_brightness,
                       ExpressionArbiter& arbiter)
    : arbiter_(arbiter), count_(count) {
    settings_ = Load(max_brightness);
    nb_mood_init(&mood_, settings_.brightness, NowMs());
    nb_conv_led_init(&conversation_, NB_CONV_HOLD_MS);

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

nb_led_settings_t MoodLights::Settings() {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return settings_;
}

void MoodLights::Apply(const nb_led_settings_t& settings) {
    nb_led_settings_t clamped = settings;
    nb_led_settings_clamp(&clamped);
    {
        std::lock_guard<std::mutex> lock(settings_mutex_);
        settings_ = clamped;
    }
    Save(clamped);  // outside the lock: NVS can take a while
}

void MoodLights::TaskEntry(void* arg) {
    static_cast<MoodLights*>(arg)->Run();
}

void MoodLights::Run() {
    while (true) {
        nb_led_settings_t settings = Settings();
        mood_.max_brightness = settings.brightness;
        nb_mood_set_expression(&mood_, arbiter_.Resolve());
        nb_rgb_t color = nb_mood_step(&mood_, NowMs());
        const nb_conv_state_t conversation = nb_conv_led_step(
            &conversation_, ConversationOf(Application::GetInstance().GetDeviceState()),
            NowMs());
        if (conversation != NB_CONV_REST) {
            color = nb_conv_led_color(conversation, settings.brightness);
            settings.fixed_color = false;  // the conversation's colors say what the robot is doing
        }
        color = nb_led_settings_resolve(&settings, color);
        for (uint32_t i = 0; i < count_; i++) {
            led_strip_set_pixel(strip_, i, color.r, color.g, color.b);
        }
        if (led_strip_refresh(strip_) != ESP_OK) {
            ESP_LOGW(TAG, "refresh failed");
        }
        vTaskDelay(pdMS_TO_TICKS(kFramePeriodMs));
    }
}
