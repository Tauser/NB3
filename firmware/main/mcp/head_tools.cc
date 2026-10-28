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

// Plans from where the head is and hands the plan to the motion task.
ReturnValue Move(MotionService& motion, nb_head_move_t move, float yaw, float pitch, int times) {
    const MotionService::Status s = motion.Read();
    if (s.state == NB_MOTION_SAFETY_STATE_FAULT) {
        return std::string("Refused: the head is stopped for safety (") + FaultMeaning(s.fault) +
               "). Tell the user; self.head.recover clears it once the cause is gone.";
    }
    for (int a = 0; a < NB_MOTION_SAFETY_AXIS_COUNT; a++) {
        if (s.out_of_range[a]) {
            return std::string("Refused: the ") + (a == 0 ? "pan" : "tilt") +
                   " servo was turned by hand past where the robot may move it. Ask the user to "
                   "turn the head back toward the middle.";
        }
    }
    const nb_head_geometry_t g = motion.Geometry();
    nb_head_plan_t plan;
    if (!nb_head_plan(&g, move, yaw, pitch, times, s.position[NB_MOTION_SAFETY_AXIS_PAN],
                      s.position[NB_MOTION_SAFETY_AXIS_TILT], &plan) ||
        !motion.Play(plan)) {
        return std::string("Refused: the head cannot move right now.");
    }
    ESP_LOGI(TAG, "%s yaw %.0f pitch %.0f x%d: %d waypoints, %lu ms%s", nb_head_move_name(move),
             yaw, pitch, times, plan.count, static_cast<unsigned long>(plan.total_ms),
             plan.clamped ? ", clamped" : "");
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "move", nb_head_move_name(move));
    cJSON_AddNumberToObject(o, "takes_ms", plan.total_ms);
    if (plan.clamped) {
        cJSON_AddStringToObject(o, "note", "the head's range ended first: it went as far as it can");
    }
    if (move == NB_HEAD_LOOK || move == NB_HEAD_CENTER) {
        const nb_head_waypoint_t& end = plan.points[plan.count - 1];
        cJSON_AddNumberToObject(o, "yaw_deg", static_cast<int>(nb_head_yaw_deg(&g, end.pan)));
        cJSON_AddNumberToObject(o, "pitch_deg", static_cast<int>(nb_head_pitch_deg(&g, end.tilt)));
    }
    return o;
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
        "degrees, temperature and supply voltage, and whether it answers. A servo marked "
        "outside_safe_range was turned by hand past where the robot may move it: the head will "
        "not move until the user turns it back toward the middle. Use it when the user "
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
                if (s.out_of_range[a]) {
                    cJSON_AddBoolToObject(servo, "outside_safe_range", true);
                }
                cJSON_AddNumberToObject(servo, "position_deg",
                                        static_cast<int>(nb_scs_counts_to_deg(s.position[a])));
                cJSON_AddNumberToObject(servo, "temperature_c", s.temperature_c[a]);
                cJSON_AddNumberToObject(servo, "voltage", s.voltage_mv[a] / 100 / 10.0);
            }
            return o;
        });

    mcp.AddTool(
        "self.head.look",
        "Turn the robot's own head to look in a direction, and keep it there. yaw_deg is left "
        "and right from the robot's point of view (positive = the robot's left, negative = its "
        "right); pitch_deg is up and down (positive = up). 0, 0 is straight ahead. The head "
        "reaches about 30 degrees to each side and 15 up or down; a bigger angle stops at the "
        "limit (the reply says so). Use it when the user asks the robot to look somewhere "
        "(\"olha pra esquerda\", \"olha pra cima\", \"olha pra mim\" = 0, 0) or to turn toward "
        "something while talking. The robot's safety may refuse or stop any move: then the "
        "reply says why; do not insist.",
        PropertyList({Property("yaw_deg", kPropertyTypeInteger, 0, -90, 90),
                      Property("pitch_deg", kPropertyTypeInteger, 0, -60, 60)}),
        [&motion](const PropertyList& p) -> ReturnValue {
            return Move(motion, NB_HEAD_LOOK, static_cast<float>(p["yaw_deg"].value<int>()),
                        static_cast<float>(p["pitch_deg"].value<int>()), 1);
        });

    mcp.AddTool(
        "self.head.gesture",
        "Make a gesture with the robot's own head: nod (yes, down and up), shake (no, side to "
        "side) or center (look straight ahead). times repeats a nod or a shake (1 to 4; 2 is "
        "natural). A nod or a shake ends where the head was. Use it to answer yes or no with "
        "the head, to agree or disagree while talking, or when the user asks (\"balança a "
        "cabeça\", \"faz que sim\", \"faz que não\", \"olha pra frente\"). Do not gesture on "
        "every sentence: once in a while, where a person would. The robot's safety may refuse "
        "or stop any move: then the reply says why; do not insist.",
        PropertyList({Property("kind", kPropertyTypeString),
                      Property("times", kPropertyTypeInteger, 2, 1, NB_HEAD_REPEATS_MAX)}),
        [&motion](const PropertyList& p) -> ReturnValue {
            const std::string kind = p["kind"].value<std::string>();
            nb_head_move_t move = NB_HEAD_NOD;
            if (!nb_head_move_from_name(kind.c_str(), &move) || move == NB_HEAD_LOOK) {
                return std::string("Unknown gesture '") + kind + "'. Use nod, shake or center.";
            }
            return Move(motion, move, 0.0f, 0.0f, p["times"].value<int>());
        });

    mcp.AddTool(
        "self.head.relax",
        "Let the robot's head go limp: the servos stop holding and the head can be moved by "
        "hand. Use it when the user wants to move the head by hand (\"solta a cabeça\", "
        "\"relaxa o pescoço\") or will not need it for a while. Always allowed; the next look "
        "or gesture holds the head again from where it is.",
        PropertyList(), [&motion](const PropertyList&) -> ReturnValue {
            motion.Relax();
            return std::string("The head is relaxed.");
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
