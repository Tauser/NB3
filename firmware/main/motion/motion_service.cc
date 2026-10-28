#include "motion_service.h"

#include <cstdlib>

#include <esp_log.h>
#include <esp_timer.h>

#include "board/config.h"

#define TAG "Motion"

namespace {

constexpr uint32_t kPeriodMs = 50;
constexpr uint32_t kLogPeriodMs = 5000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

const char* AxisName(int axis) {
    return axis == NB_MOTION_SAFETY_AXIS_PAN ? "pan" : "tilt";
}

}  // namespace

MotionService::MotionService(uint8_t pan_id, uint8_t tilt_id)
    : bus_(SERVO_UART_PORT, SERVO_UART_TX, SERVO_UART_RX, SERVO_UART_BAUD) {
    ids_[NB_MOTION_SAFETY_AXIS_PAN] = pan_id;
    ids_[NB_MOTION_SAFETY_AXIS_TILT] = tilt_id;
    const nb_motion_safety_cfg_t cfg = nb_motion_safety_default_cfg();
    nb_motion_safety_init(&safety_, &cfg);
    if (!bus_.ok()) {
        return;
    }
    if (xTaskCreate(TaskEntry, "nb_motion", 4096, this, 5, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task not created: no servos");
    }
}

MotionService::~MotionService() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
}

void MotionService::TaskEntry(void* arg) {
    static_cast<MotionService*>(arg)->Run();
}

// Pings the configured ids; if one is silent, lists who is on the bus.
void MotionService::FindServos() {
    bool all = true;
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        const bool found = bus_.Ping(ids_[a]);
        ESP_LOGI(TAG, "%s id %u: %s", AxisName(a), ids_[a], found ? "answers" : "silent");
        all = all && found;
    }
    if (all) {
        return;
    }
    int count = 0;
    for (int id = 0; id < NB_SCS_ID_BROADCAST; id++) {
        if (bus_.Ping(static_cast<uint8_t>(id))) {
            ESP_LOGW(TAG, "servo found at id %d", id);
            count++;
        }
    }
    ESP_LOGW(TAG, "bus scan: %d servo(s) answer (timeouts %lu, bad frames %lu)", count,
             static_cast<unsigned long>(bus_.timeouts()),
             static_cast<unsigned long>(bus_.bad_frames()));
}

void MotionService::LogReadings() {
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        if (!seen_[a]) {
            ESP_LOGW(TAG, "%s: no reading (%lu misses)", AxisName(a),
                     static_cast<unsigned long>(misses_[a]));
            continue;
        }
        const nb_scs_feedback_t& f = last_[a];
        ESP_LOGI(TAG, "%s: pos %u (seen %u..%u), load %d, %.1f V, %u C, error 0x%02x, %lu misses",
                 AxisName(a), f.position, min_[a], max_[a], f.load,
                 nb_scs_voltage_mv(f.voltage_dv) / 1000.0, f.temperature_c, last_error_[a],
                 static_cast<unsigned long>(misses_[a]));
    }
    switch (nb_motion_safety_get_state(&safety_)) {
        case NB_MOTION_SAFETY_STATE_INIT: ESP_LOGI(TAG, "safety: not armed"); break;
        case NB_MOTION_SAFETY_STATE_OK: ESP_LOGI(TAG, "safety: armed"); break;
        default:
            ESP_LOGW(TAG, "safety: FAULT %s on %s",
                     nb_motion_safety_fault_name(nb_motion_safety_get_fault(&safety_)),
                     AxisName(nb_motion_safety_get_fault_axis(&safety_)));
            break;
    }
}

// Servos power up holding their position. Until the safety is armed the head
// is left free, which is also what lets it be moved by hand to calibrate.
void MotionService::Relax() {
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        if (!nb_motion_safety_torque_allowed(&safety_, false, NowMs())) {
            continue;
        }
        const bool written = bus_.WriteTorque(ids_[a], false);
        bool enabled = true;
        const bool read = bus_.ReadTorque(ids_[a], &enabled);
        ESP_LOGI(TAG, "%s torque off: %s", AxisName(a),
                 !written ? "write failed" : !read ? "not confirmed" : enabled ? "still on" : "free");
    }
}

void MotionService::Run() {
    FindServos();
    Relax();
    uint32_t last_log = NowMs();
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        const uint32_t now = NowMs();
        for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
            nb_scs_feedback_t f;
            uint8_t error = 0;
            if (!bus_.ReadFeedback(ids_[a], &f, &error)) {
                misses_[a]++;
                continue;
            }
            if (!seen_[a] || f.position < min_[a]) {
                min_[a] = f.position;
            }
            if (!seen_[a] || f.position > max_[a]) {
                max_[a] = f.position;
            }
            seen_[a] = true;
            last_[a] = f;
            last_error_[a] = error;
            const nb_motion_safety_feedback_t fb = {
                f.position, static_cast<uint16_t>(std::abs(f.load)), f.temperature_c,
                static_cast<uint16_t>(nb_scs_voltage_mv(f.voltage_dv))};
            nb_motion_safety_update(&safety_, static_cast<nb_motion_safety_axis_t>(a), &fb, now);
        }
        nb_motion_safety_heartbeat(&safety_, now);
        nb_motion_safety_tick(&safety_, now);
        if (now - last_log >= kLogPeriodMs) {
            LogReadings();
            last_log = now;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
    }
}
