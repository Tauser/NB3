#include "state_led.h"

#include "application.h"

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

void ProposeState(ExpressionArbiter& arbiter, nb_expression_t expression, uint32_t ttl_ms = 0) {
    nb_proposal_t proposal = {expression, priority::kState, ttl_ms, true};
    arbiter.Propose(NB_SOURCE_STATE, proposal);
}

}  // namespace

void StateLed::OnStateChanged() {
    status_led_.OnStateChanged();

    DeviceState state = Application::GetInstance().GetDeviceState();
    const bool entered = state != last_state_;
    last_state_ = state;
    switch (state) {
        case kDeviceStateIdle:
            arbiter_.EnterIdle();
            arbiter_.Withdraw(NB_SOURCE_STATE);
            break;
        case kDeviceStateStarting:
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            ProposeState(arbiter_, NB_EXPR_THINKING);
            break;
        case kDeviceStateWifiConfiguring:
            ProposeState(arbiter_, NB_EXPR_CONFUSED);
            break;
        case kDeviceStateListening: {
            // Called again on every voice-activity change while listening.
            // xiaozhi has no state for waiting on the reply: it keeps
            // listening until the reply starts. So once the person has
            // spoken and gone quiet, the face shows it is thinking.
            const bool voice = Application::GetInstance().GetAudioService().IsVoiceDetected();
            if (entered) {
                heard_voice_ = false;
                mood_.OnListening();
            }
            if (voice) {
                heard_voice_ = true;
            }
            if (heard_voice_ && !voice) {
                ProposeState(arbiter_, NB_EXPR_THINKING, kWaitingForReplyMs);
            } else {
                ProposeState(arbiter_, NB_EXPR_LISTENING, kListeningAttentionMs);
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
