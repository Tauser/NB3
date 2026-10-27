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
constexpr uint32_t kLevelReportMs = 30000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

TouchModifier::TouchModifier(int channel, ExpressionArbiter& arbiter, MoodModifier& mood,
                             BehaviorModifier& behavior, MemoryService& memory,
                             ArcModifier& arcs)
    : arbiter_(arbiter), mood_(mood), behavior_(behavior), memory_(memory), arcs_(arcs) {
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
    // A pad that stops answering looks like nobody touching it, so its level
    // is reported now and then: the peak shows how close a touch came.
    uint32_t report_ms = NowMs();
    uint32_t peak = 0;
    uint32_t read_errors = 0;
    while (true) {
        uint32_t raw = 0;
        const bool read_ok =
            touch_channel_read_data(channel_, TOUCH_CHAN_DATA_TYPE_SMOOTH, &raw) == ESP_OK;
        if (!read_ok) {
            read_errors++;
        } else if (raw > peak) {
            peak = raw;
        }
        if (NowMs() - report_ms >= kLevelReportMs) {
            const float base = touch_.baseline;
            ESP_LOGI(TAG, "level %lu, baseline %.0f, peak %+.1f%% (press at +%.1f%%), %lu read errors",
                     static_cast<unsigned long>(raw), base,
                     base > 0 ? (static_cast<float>(peak) / base - 1.0f) * 100.0f : 0.0f,
                     touch_.cfg.press_ratio * 100.0f, static_cast<unsigned long>(read_errors));
            report_ms = NowMs();
            peak = 0;
            read_errors = 0;
        }
        if (read_ok) {
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
            arcs_.OnTap();
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
            pet_start_ms_ = last_pet_pulse_ms_;
            mood_.OnPetting();
            arcs_.SetPetting(true);
            behavior_.OnTouch(true);
            break;
        }
        case NB_TOUCH_RELEASE: {
            ESP_LOGI(TAG, "Released");
            arcs_.SetPetting(false);
            // Petting lingers briefly; a medium touch just ends.
            if (touch_.hold_sent) {
                nb_proposal_t afterglow = {NB_EXPR_LOVING, priority::kTouch, kPetAfterglowMs, true};
                arbiter_.Propose(NB_SOURCE_TOUCH, afterglow);
                memory_.OnPetting(NowMs() - pet_start_ms_);
            } else {
                arbiter_.Withdraw(NB_SOURCE_TOUCH);
            }
            behavior_.OnTouch(false);
            break;
        }
        case NB_TOUCH_STUCK: {
            // Not a hand: drop the contact without afterglow or a memory.
            ESP_LOGW(TAG, "Contact too long; new baseline %.0f", touch_.baseline);
            arcs_.SetPetting(false);
            arbiter_.Withdraw(NB_SOURCE_TOUCH);
            behavior_.OnTouch(false);
            break;
        }
        default:
            break;
    }
}
