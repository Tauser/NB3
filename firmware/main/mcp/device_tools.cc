#include "device_tools.h"

#include <cmath>
#include <string>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "mcp_server.h"

namespace {

double Round2(float v) {
    return std::round(static_cast<double>(v) * 100.0) / 100.0;
}

const char* ConnectionText(nb_link_state_t state) {
    switch (state) {
        case NB_LINK_ONLINE: return "online";
        case NB_LINK_NO_WIFI: return "no Wi-Fi: the robot cannot reach the network";
        case NB_LINK_NO_SERVER: return "no server: Wi-Fi is up but the cloud is not answering";
        default: return "starting";
    }
}

}  // namespace

void RegisterDeviceTools(LinkMonitor& link, EnergyModifier& energy, BehaviorModifier& behavior,
                         StoreService& store, const char* last_reset) {
    McpServer::GetInstance().AddTool(
        "self.device.status",
        "Read how the robot itself is doing right now. Use it when the user asks about the "
        "robot's own condition: if it is connected (\"você está conectado?\", \"estou sem "
        "internet?\"), how tired or bored it is, whether the memory card is in, how long it has "
        "been on, whether it restarted, how much memory is left. Returns connection (online, "
        "no Wi-Fi, no server), behavior state, energy and boredom from 0 to 1 (energy low means "
        "sleepy), whether it is drowsy or asleep, memory_card, uptime in seconds, why it "
        "last restarted, and free memory in KB. It changes nothing. Report only what is "
        "asked; do not read out the whole list.",
        // The tool outlives this call, so the reason text is copied in; the rest are
        // long-lived objects held by reference.
        PropertyList(), [&link, &energy, &behavior, &store, last_reset](const PropertyList&) -> ReturnValue {
            const EnergyModifier::Snapshot e = energy.Read();
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "connection", ConnectionText(link.State()));
            cJSON_AddStringToObject(o, "behavior_state",
                                    nb_behavior_state_name(behavior.State()));
            cJSON_AddNumberToObject(o, "energy", Round2(e.energy));
            cJSON_AddNumberToObject(o, "boredom", Round2(e.boredom));
            cJSON_AddBoolToObject(o, "drowsy", e.drowsy);
            cJSON_AddBoolToObject(o, "sleeping", e.sleeping);
            cJSON_AddStringToObject(o, "memory_card", store.Available() ? "present" : "missing");
            cJSON_AddNumberToObject(o, "uptime_seconds",
                                    static_cast<double>(esp_timer_get_time() / 1000000));
            cJSON_AddStringToObject(o, "last_restart_reason", last_reset);
            cJSON_AddNumberToObject(
                o, "free_memory_kb",
                static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
            cJSON_AddNumberToObject(
                o, "free_extra_memory_kb",
                static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            return o;
        });
}
