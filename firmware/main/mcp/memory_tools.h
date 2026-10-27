#ifndef NB3_MEMORY_TOOLS_H
#define NB3_MEMORY_TOOLS_H

#include "behavior/behavior_modifier.h"
#include "behavior/energy_modifier.h"
#include "behavior/mood_modifier.h"
#include "memory/memory_service.h"

// Registers the MCP tools that let the AI read and touch the robot's mood
// and its memory on the microSD (docs/VIDA.md §6).
void RegisterMemoryTools(MemoryService& memory, MoodModifier& mood, BehaviorModifier& behavior,
                         EnergyModifier& energy);

#endif  // NB3_MEMORY_TOOLS_H
