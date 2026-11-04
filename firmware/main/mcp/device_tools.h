#ifndef NB3_DEVICE_TOOLS_H
#define NB3_DEVICE_TOOLS_H

#include "behavior/behavior_modifier.h"
#include "behavior/energy_modifier.h"
#include "behavior/link_monitor.h"
#include "memory/store_service.h"

// Registers the read-only MCP tool that says how the robot is: connection,
// behavioral state, energy, card, uptime, memory and why it last restarted.
// `last_reset` is the reason text, read once at boot, and must outlive the
// tool.
void RegisterDeviceTools(LinkMonitor& link, EnergyModifier& energy, BehaviorModifier& behavior,
                         StoreService& store, const char* last_reset);

#endif  // NB3_DEVICE_TOOLS_H
