#include "face_tools.h"

#include <string>

#include "mcp_server.h"

void RegisterFaceTools(ExpressionArbiter& arbiter) {
    McpServer::GetInstance().AddTool(
        "self.face.set_expression",
        "Show a facial expression on the robot's face for a few seconds, on top of the emotion "
        "that follows the conversation. Use it when the user asks the robot to make a face "
        "(\"faz cara de bravo\", \"sorri\"), or to stress a reaction in your answer. "
        "expression must be one of: neutral, happy, excited, loving, curious, confused, "
        "embarrassed, thinking, surprised, sad, angry, sleepy, dizzy. The expression lasts "
        "duration_seconds even after the conversation ends; touching the robot briefly covers "
        "it without stopping the clock. Afterwards the face goes back to following the "
        "conversation on its own; there is no need to reset it.",
        PropertyList({Property("expression", kPropertyTypeString),
                      Property("duration_seconds", kPropertyTypeInteger, 5, 1, 60)}),
        [&arbiter](const PropertyList& properties) -> ReturnValue {
            const std::string name = properties["expression"].value<std::string>();
            nb_expression_t expression = nb_expression_from_emotion(name.c_str());
            if (name != nb_expression_name(expression)) {
                return std::string("Unknown expression '") + name +
                       "'. Use one of: neutral, happy, excited, loving, curious, confused, "
                       "embarrassed, thinking, surprised, sad, angry, sleepy, dizzy.";
            }
            const int seconds = properties["duration_seconds"].value<int>();
            // Not transient: the AI asked for a duration, so the face keeps it
            // after the conversation returns to IDLE. Touch and faults still
            // cover it, and the deadline keeps running underneath them.
            nb_proposal_t proposal = {expression, priority::kCommand,
                                      static_cast<uint32_t>(seconds) * 1000u, false};
            arbiter.Propose(NB_SOURCE_COMMAND, proposal);
            return true;
        });
}
