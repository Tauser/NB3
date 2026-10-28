#ifndef NB3_HEAD_TOOLS_H
#define NB3_HEAD_TOOLS_H

#include "behavior/head_behavior.h"
#include "motion/motion_service.h"

// Registers the MCP tools about the head's servos: look, gestures, relax,
// how they are and the explicit way out of a safety stop. The AI's moves
// pause the head's own behavior.
void RegisterHeadTools(MotionService& motion, HeadBehavior& head);

#endif  // NB3_HEAD_TOOLS_H
