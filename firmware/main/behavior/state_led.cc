#include "state_led.h"

#include <esp_log.h>

#include "application.h"

#define TAG "StateLed"

namespace {

// The listening face is attention, not a state to hold: xiaozhi keeps the
// microphone open for up to a minute after a reply, and a face stuck on
// listening would hide the mood. It lasts this long after entering listening
// or after the last voice-activity change, then the face relaxes to the mood
// while the microphone stays open.
constexpr uint32_t kListeningAttentionMs = 8000;
// Thinking after the person stops talking; if no reply comes in this long,
// the face relaxes to the mood.
constexpr uint32_t kWaitingForReplyMs = 20000;
// A wait shorter than this shows no thinking face at all.
constexpr uint32_t kThinkDelayMs = 700;

void ProposeState(ExpressionArbiter& arbiter, nb_expression_t expression, uint32_t ttl_ms = 0) {
    nb_proposal_t proposal = {expression, priority::kState, ttl_ms, true};
    arbiter.Propose(NB_SOURCE_STATE, proposal);
}

bool IsConnecting(DeviceState state) {
    return state == kDeviceStateStarting || state == kDeviceStateConnecting ||
           state == kDeviceStateActivating || state == kDeviceStateUpgrading;
}

}  // namespace

StateLed::StateLed(Led& status_led, ExpressionArbiter& arbiter, MoodModifier& mood,
                   BehaviorModifier& behavior)
    : status_led_(status_led), arbiter_(arbiter), mood_(mood), behavior_(behavior) {
    esp_timer_create_args_t args = {};
    args.callback = OnThinkTimer;
    args.arg = this;
    args.name = "nb_think";
    if (esp_timer_create(&args, &think_timer_) != ESP_OK) {
        think_timer_ = nullptr;
        ESP_LOGE(TAG, "think timer not created: waits will show no thinking face");
    }
}

StateLed::~StateLed() {
    if (think_timer_ != nullptr) {
        esp_timer_stop(think_timer_);
        esp_timer_delete(think_timer_);
    }
}

// Still waiting on the connection or on the reply to what the person said.
bool StateLed::StillWaiting() const {
    auto& app = Application::GetInstance();
    const DeviceState state = app.GetDeviceState();
    return IsConnecting(state) || (state == kDeviceStateListening && heard_voice_ &&
                                   !app.GetAudioService().IsVoiceDetected());
}

// Runs in the esp_timer task once a wait has lasted kThinkDelayMs.
void StateLed::OnThinkTimer(void* arg) {
    auto* self = static_cast<StateLed*>(arg);
    if (self->StillWaiting()) {
        ProposeState(self->arbiter_, NB_EXPR_THINKING, kWaitingForReplyMs);
    }
}

void StateLed::OnStateChanged() {
    status_led_.OnStateChanged();
    if (think_timer_ != nullptr) {
        esp_timer_stop(think_timer_);  // any change ends the wait being timed
    }

    DeviceState state = Application::GetInstance().GetDeviceState();
    const bool entered = state != last_state_;
    last_state_ = state;
    behavior_.OnDevice(state);
    switch (state) {
        case kDeviceStateIdle:
            arbiter_.Withdraw(NB_SOURCE_STATE);
            break;
        case kDeviceStateStarting:
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            arbiter_.Withdraw(NB_SOURCE_STATE);
            if (think_timer_ != nullptr) {
                esp_timer_start_once(think_timer_, kThinkDelayMs * 1000ull);
            }
            break;
        case kDeviceStateWifiConfiguring:
            ProposeState(arbiter_, NB_EXPR_CONFUSED);
            break;
        case kDeviceStateListening: {
            // Called again on every voice-activity change while listening.
            // xiaozhi has no state for waiting on the reply: it keeps
            // listening until the reply starts. So once the person has
            // spoken and gone quiet, the face shows it is thinking. This
            // needs the local VAD to report the silence; with the current
            // noisy mic it rarely does, and the server starts the reply the
            // moment it hears the end of speech.
            const bool voice = Application::GetInstance().GetAudioService().IsVoiceDetected();
            if (entered) {
                heard_voice_ = false;
                mood_.OnListening();
            }
            if (voice) {
                heard_voice_ = true;
            }
            ProposeState(arbiter_, NB_EXPR_LISTENING, kListeningAttentionMs);
            if (heard_voice_ && !voice && think_timer_ != nullptr) {
                esp_timer_start_once(think_timer_, kThinkDelayMs * 1000ull);
            }
            break;
        }
        case kDeviceStateFatalError: {
            nb_proposal_t fault = {NB_EXPR_SAD, priority::kFault, 0, false};
            arbiter_.Propose(NB_SOURCE_STATE, fault);
            if (entered) {
                mood_.OnFault();
            }
            break;
        }
        default:
            // Speaking follows the conversation's emotion.
            arbiter_.Withdraw(NB_SOURCE_STATE);
            break;
    }
}
