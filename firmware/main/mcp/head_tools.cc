#include "head_tools.h"

#include <string>

#include <cJSON.h>
#include <esp_log.h>

#include "core/nb_scs.h"
#include "mcp_server.h"

#define TAG "HeadTools"

namespace {

const char* StateName(nb_motion_safety_state_t state) {
    switch (state) {
        case NB_MOTION_SAFETY_STATE_INIT: return "free";
        case NB_MOTION_SAFETY_STATE_OK: return "holding";
        default: return "stopped_for_safety";
    }
}

// What each stop means, in words the AI can pass on.
const char* FaultMeaning(nb_motion_safety_fault_t fault) {
    switch (fault) {
        case NB_MOTION_SAFETY_FAULT_STALL_TRACKING:
            return "a servo could not reach where it was sent: something held or blocked the head";
        case NB_MOTION_SAFETY_FAULT_STALL_LOAD:
            return "a servo pushed too hard for too long: the head was held or is stuck";
        case NB_MOTION_SAFETY_FAULT_OVER_TEMP: return "a servo got too hot";
        case NB_MOTION_SAFETY_FAULT_BROWNOUT: return "the servo power is too low or off";
        case NB_MOTION_SAFETY_FAULT_OVER_VOLTAGE: return "the servo power is too high";
        case NB_MOTION_SAFETY_FAULT_HEARTBEAT: return "the motion task stopped running";
        case NB_MOTION_SAFETY_FAULT_FEEDBACK_LOST: return "a servo stopped answering on the bus";
        case NB_MOTION_SAFETY_FAULT_CONFIG:
            return "the head is outside its safe range or the setup is wrong; it needs a restart";
        default: return "none";
    }
}

}  // namespace

void RegisterHeadTools(MotionService& motion) {
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool(
        "self.head.status",
        "Read the state of the robot's head (two servos: pan turns it left and right, tilt "
        "nods it up and down). state is free (servos relaxed, the head can be moved by hand), "
        "holding (servos powered, keeping a pose) or stopped_for_safety (the robot's own safety "
        "cut the power to the servos; reason says why). Also gives each servo's position in "
        "degrees, temperature and supply voltage, and whether it answers. Use it when the user "
        "asks about the head or neck (\"sua cabeça tá ok?\", \"por que você parou de mexer?\") "
        "or before recovering from a stop.",
        PropertyList(), [&motion](const PropertyList&) -> ReturnValue {
            const MotionService::Status s = motion.Read();
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "state", StateName(s.state));
            if (s.state == NB_MOTION_SAFETY_STATE_FAULT) {
                cJSON_AddStringToObject(o, "reason", FaultMeaning(s.fault));
                cJSON_AddStringToObject(o, "fault", nb_motion_safety_fault_name(s.fault));
                cJSON_AddStringToObject(o, "servo",
                                        s.fault_axis == NB_MOTION_SAFETY_AXIS_PAN ? "pan" : "tilt");
            }
            static const char* const kNames[] = {"pan", "tilt"};
            for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
                cJSON* servo = cJSON_AddObjectToObject(o, kNames[a]);
                cJSON_AddBoolToObject(servo, "answers", s.answering[a]);
                cJSON_AddNumberToObject(servo, "position_deg",
                                        static_cast<int>(nb_scs_counts_to_deg(s.position[a])));
                cJSON_AddNumberToObject(servo, "temperature_c", s.temperature_c[a]);
                cJSON_AddNumberToObject(servo, "voltage", s.voltage_mv[a] / 100 / 10.0);
            }
            return o;
        });

    mcp.AddTool(
        "self.head.recover",
        "Clear the head's safety stop so the servos may move again. Only when "
        "self.head.status says stopped_for_safety AND the user says the cause is gone (they "
        "let go of the head, removed what blocked it, the servo cooled down, the power is back). "
        "Never call it on your own just to keep moving: the stop protects the servos and "
        "fingers. The safety itself refuses while the cause is still measured (still hot, "
        "still pushing, power still wrong); then tell the user what is still wrong. After a "
        "recovery the head stays free until it is asked to move again.",
        PropertyList(), [&motion](const PropertyList&) -> ReturnValue {
            const MotionService::RecoverResult r = motion.Recover();
            ESP_LOGI(TAG, "recover: %d", static_cast<int>(r));
            switch (r) {
                case MotionService::RecoverResult::kRecovered:
                    return std::string("Recovered: the head may move again.");
                case MotionService::RecoverResult::kNotFaulted:
                    return std::string("The head was not stopped: nothing to recover.");
                case MotionService::RecoverResult::kRefused: {
                    const MotionService::Status s = motion.Read();
                    return std::string("Refused by the safety: ") + FaultMeaning(s.fault) +
                           ", and it is still measured. Ask the user to fix it first.";
                }
                default:
                    return std::string("The motion task did not answer: the head stays stopped.");
            }
        });
}
