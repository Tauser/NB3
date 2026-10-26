#include "behavior_modifier.h"

#include <esp_log.h>

#define TAG "Behavior"

namespace {

// Often enough to end a touch reaction on time; nothing else is polled.
constexpr uint64_t kTickPeriodUs = 200 * 1000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

nb_device_activity_t ActivityOf(DeviceState state) {
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            return NB_DEVICE_IDLE;
        case kDeviceStateFatalError:
            return NB_DEVICE_FAULT;
        default:
            return NB_DEVICE_BUSY;
    }
}

}  // namespace

BehaviorModifier::BehaviorModifier(ExpressionArbiter& arbiter) : arbiter_(arbiter) {
    nb_behavior_init(&behavior_);
    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_behavior";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kTickPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "tick timer not running: touch reactions will not end on their own");
    }
}

BehaviorModifier::~BehaviorModifier() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void BehaviorModifier::Apply(const nb_behavior_transition_t& transition) {
    if (!transition.changed) {
        return;
    }
    ESP_LOGI(TAG, "%s -> %s", nb_behavior_state_name(transition.from),
             nb_behavior_state_name(transition.to));
    if (transition.to == NB_BEHAVIOR_IDLE) {
        arbiter_.EnterIdle();
    }
}

void BehaviorModifier::OnDevice(DeviceState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    Apply(nb_behavior_device(&behavior_, ActivityOf(state), NowMs()));
}

void BehaviorModifier::OnTouch(bool touching) {
    std::lock_guard<std::mutex> lock(mutex_);
    Apply(nb_behavior_touch(&behavior_, touching, NowMs()));
}

void BehaviorModifier::Sleep() {
    std::lock_guard<std::mutex> lock(mutex_);
    Apply(nb_behavior_sleep(&behavior_));
}

void BehaviorModifier::Wake() {
    std::lock_guard<std::mutex> lock(mutex_);
    Apply(nb_behavior_wake(&behavior_));
}

void BehaviorModifier::Recover() {
    std::lock_guard<std::mutex> lock(mutex_);
    Apply(nb_behavior_recover(&behavior_, NowMs()));
}

void BehaviorModifier::SetQuiet(bool quiet) {
    std::lock_guard<std::mutex> lock(mutex_);
    nb_behavior_set_quiet(&behavior_, quiet);
}

nb_behavior_state_t BehaviorModifier::State() {
    std::lock_guard<std::mutex> lock(mutex_);
    return behavior_.state;
}

void BehaviorModifier::OnTimer(void* arg) {
    auto* self = static_cast<BehaviorModifier*>(arg);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->Apply(nb_behavior_tick(&self->behavior_, NowMs()));
}
