#include "touch_modifier.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "Touch"

namespace {

constexpr uint32_t kSamplePeriodMs = 20;
constexpr uint32_t kSettleMs = 200;           // pad must be untouched while settling
constexpr uint32_t kTapReactionMs = 1500;
constexpr uint32_t kPetAfterglowMs = 2000;
constexpr uint32_t kPetPulseMs = 4000;  // petting warms the mood again this often

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

TouchModifier::TouchModifier(int channel, ExpressionArbiter& arbiter, MoodModifier& mood,
                             BehaviorModifier& behavior)
    : arbiter_(arbiter), mood_(mood), behavior_(behavior) {
    nb_touch_config_t cfg = nb_touch_default_config();
    nb_touch_init(&touch_, &cfg);

    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
    };
    touch_sensor_config_t sensor_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sensor_cfg, &sensor_));

    // Hardware activation is left inert (high threshold): detection runs in
    // nb_touch on the smoothed reading, where it can be host-tested.
    touch_channel_config_t channel_cfg = {};
    channel_cfg.active_thresh[0] = 2000;
    channel_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
    channel_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
    ESP_ERROR_CHECK(touch_sensor_new_channel(sensor_, channel, &channel_cfg, &channel_));

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(sensor_, &filter_cfg));
    ESP_ERROR_CHECK(touch_sensor_enable(sensor_));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sensor_));

    xTaskCreate(TaskEntry, "nb_touch", 3072, this, 4, &task_);
}

TouchModifier::~TouchModifier() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
}

void TouchModifier::TaskEntry(void* arg) {
    static_cast<TouchModifier*>(arg)->Run();
}

void TouchModifier::Run() {
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));
    bool reported_baseline = false;
    while (true) {
        uint32_t raw = 0;
        if (touch_channel_read_data(channel_, TOUCH_CHAN_DATA_TYPE_SMOOTH, &raw) == ESP_OK) {
            nb_touch_event_t event = nb_touch_update(&touch_, raw, NowMs());
            if (!reported_baseline && nb_touch_is_calibrated(&touch_)) {
                ESP_LOGI(TAG, "Calibrated, baseline %.0f", touch_.baseline);
                reported_baseline = true;
            }
            if (event != NB_TOUCH_NONE) {
                OnEvent(event);
            }
            const uint32_t now = NowMs();
            if (touch_.hold_sent && touch_.touched && now - last_pet_pulse_ms_ >= kPetPulseMs) {
                last_pet_pulse_ms_ = now;
                mood_.OnPetting();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kSamplePeriodMs));
    }
}

void TouchModifier::OnEvent(nb_touch_event_t event) {
    switch (event) {
        case NB_TOUCH_TAP: {
            ESP_LOGI(TAG, "Tap");
            nb_proposal_t happy = {NB_EXPR_HAPPY, priority::kTouch, kTapReactionMs, true};
            arbiter_.Propose(NB_SOURCE_TOUCH, happy);
            mood_.OnTap();
            // A tap is known on release: the reaction starts and lingers.
            behavior_.OnTouch(true);
            behavior_.OnTouch(false);
            break;
        }
        case NB_TOUCH_HOLD: {
            ESP_LOGI(TAG, "Petting");
            nb_proposal_t loving = {NB_EXPR_LOVING, priority::kTouch, 0, true};
            arbiter_.Propose(NB_SOURCE_TOUCH, loving);
            last_pet_pulse_ms_ = NowMs();
            mood_.OnPetting();
            behavior_.OnTouch(true);
            break;
        }
        case NB_TOUCH_RELEASE: {
            ESP_LOGI(TAG, "Released");
            // Petting lingers briefly; a medium touch just ends.
            if (touch_.hold_sent) {
                nb_proposal_t afterglow = {NB_EXPR_LOVING, priority::kTouch, kPetAfterglowMs, true};
                arbiter_.Propose(NB_SOURCE_TOUCH, afterglow);
            } else {
                arbiter_.Withdraw(NB_SOURCE_TOUCH);
            }
            behavior_.OnTouch(false);
            break;
        }
        default:
            break;
    }
}
