#include "nb_link.h"

#include <string.h>

static const char *const kNames[] = {"starting", "online", "no_wifi", "no_server"};

const char *nb_link_state_name(nb_link_state_t state) {
    return (unsigned)state < sizeof(kNames) / sizeof(kNames[0]) ? kNames[state] : "unknown";
}

void nb_link_init(nb_link_t *l, uint32_t now_ms) {
    memset(l, 0, sizeof(*l));
    l->state = NB_LINK_STARTING;
    l->wifi_down_ms = now_ms;
    l->since_ms = now_ms;
}

static bool go(nb_link_t *l, nb_link_state_t state, uint32_t now_ms) {
    if (l->state == state) {
        return false;
    }
    l->state = state;
    l->since_ms = now_ms;
    return true;
}

bool nb_link_update(nb_link_t *l, bool wifi_up, bool server_ok, bool server_error,
                    uint32_t now_ms) {
    if (wifi_up != l->wifi_up) {
        l->wifi_up = wifi_up;
        if (!wifi_up) {
            l->wifi_down_ms = now_ms;
        }
    }
    if (!wifi_up) {
        // Only a Wi-Fi seen before can be lost; at boot it is still starting.
        const bool long_enough = (uint32_t)(now_ms - l->wifi_down_ms) >= NB_LINK_WIFI_GRACE_MS;
        return l->wifi_seen && long_enough ? go(l, NB_LINK_NO_WIFI, now_ms) : false;
    }
    const bool came_back = !l->wifi_seen || l->state == NB_LINK_NO_WIFI;
    l->wifi_seen = true;
    if (server_ok) {
        return go(l, NB_LINK_ONLINE, now_ms);  // the conversation proves it
    }
    if (server_error) {
        return go(l, NB_LINK_NO_SERVER, now_ms);
    }
    if (came_back || l->state == NB_LINK_STARTING) {
        return go(l, NB_LINK_ONLINE, now_ms);  // a fresh address: a new chance
    }
    return false;
}
