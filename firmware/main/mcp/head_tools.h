#ifndef NB3_HEAD_TOOLS_H
#define NB3_HEAD_TOOLS_H

#include "motion/motion_service.h"

// Registers the MCP tools about the head's servos: how they are and the
// explicit way out of a safety stop.
void RegisterHeadTools(MotionService& motion);

#endif  // NB3_HEAD_TOOLS_H
