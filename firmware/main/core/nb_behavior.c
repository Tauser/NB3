#include "nb_behavior.h"

#include <stddef.h>
#include <string.h>

static const char *const kNames[NB_BEHAVIOR_COUNT] = {
    [NB_BEHAVIOR_IDLE] = "idle",         [NB_BEHAVIOR_ENGAGED] = "engaged",
    [NB_BEHAVIOR_REACTING] = "reacting", [NB_BEHAVIOR_SLEEPING] = "sleeping",
    [NB_BEHAVIOR_FAULT] = "fault",
};

static nb_behavior_transition_t go(nb_behavior_t *b, nb_behavior_state_t to) {
    nb_behavior_transition_t t = {b->state != to, b->state, to};
    b->state = to;
    return t;
}

static nb_behavior_transition_t none(const nb_behavior_t *b) {
    nb_behavior_transition_t t = {false, b->state, b->state};
    return t;
}

// Where the robot settles when nothing forces a state: the conversation wins
// over the hand, the hand (or its lingering reaction) over rest.
static nb_behavior_state_t settle(const nb_behavior_t *b) {
    if (b->device == NB_DEVICE_BUSY) {
        return NB_BEHAVIOR_ENGAGED;
    }
    if (b->touching || b->reaction_lingering) {
        return NB_BEHAVIOR_REACTING;
    }
    return NB_BEHAVIOR_IDLE;
}

void nb_behavior_init(nb_behavior_t *b) {
    if (b != NULL) {
        memset(b, 0, sizeof(*b));
        b->state = NB_BEHAVIOR_IDLE;
        b->device = NB_DEVICE_IDLE;
    }
}

nb_behavior_transition_t nb_behavior_device(nb_behavior_t *b, nb_device_activity_t device,
                                            uint32_t now_ms) {
    (void)now_ms;
    nb_behavior_transition_t t = {false, NB_BEHAVIOR_IDLE, NB_BEHAVIOR_IDLE};
    if (b == NULL || (int)device < 0 || device > NB_DEVICE_FAULT) {
        return t;
    }
    b->device = device;
    if (device == NB_DEVICE_FAULT) {
        return go(b, NB_BEHAVIOR_FAULT);
    }
    if (b->state == NB_BEHAVIOR_FAULT) {
        return none(b);
    }
    if (b->state == NB_BEHAVIOR_SLEEPING && device == NB_DEVICE_IDLE) {
        return none(b);
    }
    return go(b, settle(b));
}

nb_behavior_transition_t nb_behavior_touch(nb_behavior_t *b, bool touching, uint32_t now_ms) {
    nb_behavior_transition_t t = {false, NB_BEHAVIOR_IDLE, NB_BEHAVIOR_IDLE};
    if (b == NULL) {
        return t;
    }
    b->touching = touching;
    if (touching) {
        b->reaction_lingering = false;
    } else {
        b->reaction_lingering = true;
        b->reaction_until_ms = now_ms + NB_BEHAVIOR_REACTION_LINGER_MS;
    }
    if (b->state == NB_BEHAVIOR_FAULT) {
        return none(b);
    }
    return go(b, settle(b));
}

nb_behavior_transition_t nb_behavior_sleep(nb_behavior_t *b) {
    nb_behavior_transition_t t = {false, NB_BEHAVIOR_IDLE, NB_BEHAVIOR_IDLE};
    if (b == NULL) {
        return t;
    }
    return b->state == NB_BEHAVIOR_IDLE ? go(b, NB_BEHAVIOR_SLEEPING) : none(b);
}

nb_behavior_transition_t nb_behavior_wake(nb_behavior_t *b) {
    nb_behavior_transition_t t = {false, NB_BEHAVIOR_IDLE, NB_BEHAVIOR_IDLE};
    if (b == NULL) {
        return t;
    }
    return b->state == NB_BEHAVIOR_SLEEPING ? go(b, settle(b)) : none(b);
}

nb_behavior_transition_t nb_behavior_recover(nb_behavior_t *b, uint32_t now_ms) {
    (void)now_ms;
    nb_behavior_transition_t t = {false, NB_BEHAVIOR_IDLE, NB_BEHAVIOR_IDLE};
    if (b == NULL) {
        return t;
    }
    if (b->state != NB_BEHAVIOR_FAULT || b->device == NB_DEVICE_FAULT) {
        return none(b);  // the cause is still there
    }
    return go(b, settle(b));
}

nb_behavior_transition_t nb_behavior_tick(nb_behavior_t *b, uint32_t now_ms) {
    nb_behavior_transition_t t = {false, NB_BEHAVIOR_IDLE, NB_BEHAVIOR_IDLE};
    if (b == NULL) {
        return t;
    }
    if (b->reaction_lingering && (int32_t)(now_ms - b->reaction_until_ms) >= 0) {
        b->reaction_lingering = false;
        if (b->state == NB_BEHAVIOR_REACTING) {
            return go(b, settle(b));
        }
    }
    return none(b);
}

void nb_behavior_set_quiet(nb_behavior_t *b, bool quiet) {
    if (b != NULL) {
        b->quiet = quiet;
    }
}

const char *nb_behavior_state_name(nb_behavior_state_t state) {
    return ((int)state >= 0 && state < NB_BEHAVIOR_COUNT) ? kNames[state] : "unknown";
}
