#ifndef NB_BEHAVIOR_H
#define NB_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The robot's behavioral state (docs/VIDA.md §2): what it is doing, kept
// apart from how it feels (the mood) and from xiaozhi's conversation states,
// which it only reads. Entering IDLE is the moment to clear everything
// transient; the mood is not part of this and survives it.

typedef enum {
    NB_BEHAVIOR_IDLE = 0,  // at rest
    NB_BEHAVIOR_ENGAGED,   // in a conversation (connecting, listening, speaking)
    NB_BEHAVIOR_REACTING,  // reacting to a touch while at rest
    NB_BEHAVIOR_SLEEPING,
    NB_BEHAVIOR_FAULT,     // sticky until an explicit recovery
    NB_BEHAVIOR_COUNT
} nb_behavior_state_t;

// What xiaozhi is doing, folded to what matters here.
typedef enum {
    NB_DEVICE_IDLE = 0,
    NB_DEVICE_BUSY,   // starting, connecting, listening, speaking, setup, upgrade
    NB_DEVICE_FAULT,
} nb_device_activity_t;

// A touch reaction lasts this long after the hand leaves.
#define NB_BEHAVIOR_REACTION_LINGER_MS 2000u

typedef struct {
    bool changed;
    nb_behavior_state_t from;
    nb_behavior_state_t to;
} nb_behavior_transition_t;

typedef struct {
    nb_behavior_state_t state;
    nb_device_activity_t device;
    bool touching;
    bool reaction_lingering;
    uint32_t reaction_until_ms;
    bool quiet;  // a mode, not a state: survives every transition
} nb_behavior_t;

void nb_behavior_init(nb_behavior_t *b);

// Each event returns the transition it caused, if any.
nb_behavior_transition_t nb_behavior_device(nb_behavior_t *b, nb_device_activity_t device,
                                            uint32_t now_ms);
nb_behavior_transition_t nb_behavior_touch(nb_behavior_t *b, bool touching, uint32_t now_ms);
// Only from IDLE; anything that engages the robot wakes it.
nb_behavior_transition_t nb_behavior_sleep(nb_behavior_t *b);
nb_behavior_transition_t nb_behavior_wake(nb_behavior_t *b);
// Leaves FAULT for whatever the device and the hand say now.
nb_behavior_transition_t nb_behavior_recover(nb_behavior_t *b, uint32_t now_ms);
// Ends a lingering touch reaction when its time is up.
nb_behavior_transition_t nb_behavior_tick(nb_behavior_t *b, uint32_t now_ms);

void nb_behavior_set_quiet(nb_behavior_t *b, bool quiet);

const char *nb_behavior_state_name(nb_behavior_state_t state);

#ifdef __cplusplus
}
#endif

#endif  // NB_BEHAVIOR_H
