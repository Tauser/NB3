#ifndef NB_LINK_H
#define NB_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Whether the robot can reach its mind in the cloud (docs/ROADMAP.md stage
// 5). The shell samples whether the Wi-Fi has an address and reports what
// the conversation shows: a server error, or a conversation that got going.
// The rest of the robot keeps living either way; this only decides what to
// say about it and whether to hand things to the AI.
//
// - No Wi-Fi: the address gone for longer than a short blip.
// - No server: a server error with the Wi-Fi up, until a conversation gets
//   going again or the Wi-Fi comes back (a new chance).
// - Starting: nothing known yet after boot; counts as offline for handing
//   things to the AI, but nothing is announced.

#define NB_LINK_WIFI_GRACE_MS 5000u  // a shorter loss is a blip

typedef enum {
    NB_LINK_STARTING = 0,
    NB_LINK_ONLINE,
    NB_LINK_NO_WIFI,
    NB_LINK_NO_SERVER,
} nb_link_state_t;

typedef struct {
    nb_link_state_t state;
    bool wifi_up;
    bool wifi_seen;          // the Wi-Fi was up at least once
    uint32_t wifi_down_ms;   // when it went down (valid while !wifi_up)
    uint32_t since_ms;       // when the state last changed
} nb_link_t;

void nb_link_init(nb_link_t *l, uint32_t now_ms);

// One sample. server_ok / server_error: since the last update the
// conversation got going / the server failed. Returns true when the state
// changed.
bool nb_link_update(nb_link_t *l, bool wifi_up, bool server_ok, bool server_error,
                    uint32_t now_ms);

static inline bool nb_link_online(const nb_link_t *l) {
    return l->state == NB_LINK_ONLINE;
}

const char *nb_link_state_name(nb_link_state_t state);

#ifdef __cplusplus
}
#endif

#endif  // NB_LINK_H
