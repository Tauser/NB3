#include "motion_service.h"

#include <cstdlib>

#include <esp_log.h>
#include <esp_timer.h>

#include "board/config.h"

#define TAG "Motion"

namespace {

constexpr uint32_t kPeriodMs = 50;
constexpr uint32_t kLogPeriodMs = 5000;
constexpr uint32_t kFaultFaceMs = 3000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

const char* AxisName(int axis) {
    return axis == NB_MOTION_SAFETY_AXIS_PAN ? "pan" : "tilt";
}

}  // namespace

MotionService::MotionService(uint8_t pan_id, uint8_t tilt_id, ExpressionArbiter* arbiter)
    : bus_(SERVO_UART_PORT, SERVO_UART_TX, SERVO_UART_RX, SERVO_UART_BAUD), arbiter_(arbiter) {
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
// Only a release confirmed on both servos tells the safety no goal is held.
bool MotionService::Relax() {
    bool all_free = true;
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        if (!nb_motion_safety_torque_allowed(&safety_, false, NowMs())) {
            all_free = false;
            continue;
        }
        const bool written = bus_.WriteTorque(ids_[a], false);
        bool enabled = true;
        const bool read = bus_.ReadTorque(ids_[a], &enabled);
        const bool free = written && read && !enabled;
        all_free = all_free && free;
        ESP_LOGI(TAG, "%s torque off: %s", AxisName(a),
                 !written ? "write failed" : !read ? "not confirmed" : enabled ? "still on" : "free");
    }
    if (all_free) {
        nb_motion_safety_released(&safety_);
    }
    return all_free;
}

MotionService::Status MotionService::Read() {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

MotionService::RecoverResult MotionService::Recover() {
    recover_result_ = kRecoverPending;
    recover_asked_ = true;
    for (int i = 0; i < 50 && recover_result_ == kRecoverPending; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    const int r = recover_result_.exchange(kRecoverPending);
    return r == kRecoverPending ? RecoverResult::kNoAnswer : static_cast<RecoverResult>(r);
}

void MotionService::HandleRecover(uint32_t now_ms) {
    if (!recover_asked_.exchange(false)) {
        return;
    }
    if (nb_motion_safety_get_state(&safety_) != NB_MOTION_SAFETY_STATE_FAULT) {
        recover_result_ = static_cast<int>(RecoverResult::kNotFaulted);
        return;
    }
    const nb_motion_safety_fault_t was = nb_motion_safety_get_fault(&safety_);
    if (!nb_motion_safety_reset_fault(&safety_, now_ms)) {
        ESP_LOGW(TAG, "recover refused: %s still present", nb_motion_safety_fault_name(was));
        recover_result_ = static_cast<int>(RecoverResult::kRefused);
        return;
    }
    ESP_LOGI(TAG, "recovered from %s: the next goal arms again", nb_motion_safety_fault_name(was));
    fault_logged_ = false;
    recover_result_ = static_cast<int>(RecoverResult::kRecovered);
}

void MotionService::PublishStatus() {
    Status s;
    s.state = nb_motion_safety_get_state(&safety_);
    s.fault = nb_motion_safety_get_fault(&safety_);
    s.fault_axis = nb_motion_safety_get_fault_axis(&safety_);
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        s.answering[a] = seen_[a] && misses_[a] == last_misses_[a];
        s.position[a] = last_[a].position;
        s.out_of_range[a] =
            seen_[a] && nb_motion_safety_clamp_position(
                            &safety_, static_cast<nb_motion_safety_axis_t>(a), last_[a].position) !=
                            last_[a].position;
        s.temperature_c[a] = last_[a].temperature_c;
        s.voltage_mv[a] = static_cast<uint16_t>(nb_scs_voltage_mv(last_[a].voltage_dv));
        last_misses_[a] = misses_[a];
    }
    state_ = s.state;
    fault_ = s.fault;
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = s;
}

void MotionService::RequestGoal(nb_motion_safety_axis_t axis, uint16_t position,
                                uint16_t time_ms) {
    if (static_cast<int>(axis) < 0 || static_cast<int>(axis) >= NB_MOTION_SAFETY_AXIS_COUNT) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    goals_[axis] = {true, position, time_ms};
}

nb_motion_safety_state_t MotionService::State() {
    return static_cast<nb_motion_safety_state_t>(state_.load());
}

nb_motion_safety_fault_t MotionService::Fault() {
    return static_cast<nb_motion_safety_fault_t>(fault_.load());
}

// INIT -> OK on fresh readings, then each servo's goal is set to where it
// already is before the torque comes on, so holding never starts with a jump
// to whatever goal the servo kept from before.
bool MotionService::Arm(uint32_t now_ms) {
    // A free servo moved by hand past its limits is not a fault: holding it
    // there would be, so arming waits until it is put back.
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        const auto axis = static_cast<nb_motion_safety_axis_t>(a);
        const uint16_t here = last_[a].position;
        if (nb_motion_safety_clamp_position(&safety_, axis, here) != here) {
            ESP_LOGW(TAG, "not arming: %s at %u is outside its safe range %u..%u: put it back by hand",
                     AxisName(a), here, safety_.cfg.pos_min[a], safety_.cfg.pos_max[a]);
            return false;
        }
    }
    if (!nb_motion_safety_arm(&safety_, now_ms)) {
        return false;
    }
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        const auto axis = static_cast<nb_motion_safety_axis_t>(a);
        const uint16_t here = last_[a].position;
        const nb_motion_safety_cmd_result_t r = nb_motion_safety_command(&safety_, axis, here, now_ms);
        if (r != NB_MOTION_SAFETY_CMD_OK) {
            // Moved out between the check and now, or no longer OK.
            ESP_LOGW(TAG, "not arming: %s at %u refused (%d)", AxisName(a), here, r);
            nb_motion_safety_trip(&safety_, NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, axis);
            return false;
        }
        const nb_scs_goal_t goal = {ids_[a], here, 0, 0};
        if (!bus_.WriteGoal(goal)) {
            // Armed but unable to hold: leave OK the only way there is.
            ESP_LOGW(TAG, "not arming: %s goal preload failed", AxisName(a));
            nb_motion_safety_trip(&safety_, NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST, axis);
            return false;
        }
    }
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        if (!nb_motion_safety_torque_allowed(&safety_, true, now_ms) ||
            !bus_.WriteTorque(ids_[a], true)) {
            ESP_LOGW(TAG, "not arming: %s torque on refused or failed", AxisName(a));
            nb_motion_safety_trip(&safety_, NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST,
                                  static_cast<nb_motion_safety_axis_t>(a));
            torque_on_ = true;  // the fault path releases both
            return false;
        }
    }
    torque_on_ = true;
    ESP_LOGI(TAG, "armed: holding pan %u, tilt %u", last_[NB_MOTION_SAFETY_AXIS_PAN].position,
             last_[NB_MOTION_SAFETY_AXIS_TILT].position);
    return true;
}

void MotionService::ApplyGoals(uint32_t now_ms) {
    Goal goals[NB_MOTION_SAFETY_AXIS_COUNT];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
            goals[a] = goals_[a];
            goals_[a].pending = false;
        }
    }
    if (!goals[0].pending && !goals[1].pending) {
        return;
    }
    if (nb_motion_safety_get_state(&safety_) == NB_MOTION_SAFETY_STATE_INIT && !Arm(now_ms)) {
        return;  // the goals are dropped: asking again is the caller's choice
    }
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        if (!goals[a].pending) {
            continue;
        }
        const auto axis = static_cast<nb_motion_safety_axis_t>(a);
        const nb_motion_safety_cmd_result_t r =
            nb_motion_safety_command(&safety_, axis, goals[a].position, now_ms);
        if (r != NB_MOTION_SAFETY_CMD_OK) {
            ESP_LOGW(TAG, "%s to %u refused (%d)", AxisName(a), goals[a].position, r);
            continue;
        }
        const nb_scs_goal_t goal = {ids_[a], goals[a].position, goals[a].time_ms, 0};
        if (!bus_.WriteGoal(goal)) {
            ESP_LOGW(TAG, "%s to %u: write failed", AxisName(a), goals[a].position);
        }
    }
}

void MotionService::Run() {
    FindServos();
    torque_on_ = !Relax();
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
        if (nb_motion_safety_get_state(&safety_) == NB_MOTION_SAFETY_STATE_FAULT) {
            if (torque_on_) {
                torque_on_ = !Relax();  // tried again every cycle until both are free
            }
            if (!fault_logged_) {
                fault_logged_ = true;
                ESP_LOGE(TAG, "FAULT %s on %s: servos released until recovered",
                         nb_motion_safety_fault_name(nb_motion_safety_get_fault(&safety_)),
                         AxisName(nb_motion_safety_get_fault_axis(&safety_)));
                LogReadings();
                if (arbiter_ != nullptr) {
                    // A start, not a mood: the face says it, then the robot goes on.
                    nb_proposal_t dizzy = {NB_EXPR_DIZZY, priority::kFault, kFaultFaceMs, false};
                    arbiter_->Propose(NB_SOURCE_REFLEX, dizzy);
                }
            }
            if (!torque_on_) {
                HandleRecover(now);
            }
        } else {
            HandleRecover(now);
            ApplyGoals(now);
        }
        PublishStatus();
        if (now - last_log >= kLogPeriodMs) {
            LogReadings();
            last_log = now;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
    }
}
