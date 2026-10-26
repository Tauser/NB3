#include "state_led.h"

#include "application.h"

namespace {

void ProposeState(ExpressionArbiter& arbiter, nb_expression_t expression) {
    nb_proposal_t proposal = {expression, priority::kState, 0, true};
    arbiter.Propose(NB_SOURCE_STATE, proposal);
}

}  // namespace

void StateLed::OnStateChanged() {
    status_led_.OnStateChanged();

    DeviceState state = Application::GetInstance().GetDeviceState();
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
        case kDeviceStateListening:
            ProposeState(arbiter_, NB_EXPR_LISTENING);
            break;
        case kDeviceStateFatalError: {
            nb_proposal_t fault = {NB_EXPR_SAD, priority::kFault, 0, false};
            arbiter_.Propose(NB_SOURCE_STATE, fault);
            break;
        }
        default:
            // Speaking follows the conversation's emotion.
            arbiter_.Withdraw(NB_SOURCE_STATE);
            break;
    }
}
