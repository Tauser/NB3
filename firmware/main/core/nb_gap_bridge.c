#include "nb_gap_bridge.h"

#include <stddef.h>
#include <string.h>

void nb_gap_bridge_init(nb_gap_bridge_t *bridge, const nb_gap_bridge_config_t *cfg) {
    if (bridge == NULL || cfg == NULL) {
        return;
    }
    memset(bridge, 0, sizeof(*bridge));
    bridge->cfg = *cfg;
}

void nb_gap_bridge_reset(nb_gap_bridge_t *bridge) {
    if (bridge == NULL) {
        return;
    }
    bridge->heard_speech = false;
    bridge->silence_ms = 0u;
    bridge->holding = false;
}

nb_gap_action_t nb_gap_bridge_step(nb_gap_bridge_t *bridge, bool speech, uint32_t frame_ms) {
    if (bridge == NULL || bridge->cfg.max_hold_ms == 0u) {
        return NB_GAP_SEND;
    }
    if (speech) {
        const bool resuming = bridge->holding;
        bridge->heard_speech = true;
        bridge->silence_ms = 0u;
        bridge->holding = false;
        return resuming ? NB_GAP_FLUSH_AND_SEND : NB_GAP_SEND;
    }
    if (!bridge->heard_speech) {
        return NB_GAP_SEND;  // before the person starts there is no pause to hide
    }
    bridge->silence_ms += frame_ms;
    if (bridge->silence_ms <= bridge->cfg.max_hold_ms) {
        bridge->holding = true;
        return NB_GAP_HOLD;
    }
    // The silence is the end of the turn, not a breath: let the server see it.
    bridge->holding = false;
    return NB_GAP_SEND;
}

uint32_t nb_gap_bridge_preroll_frames(const nb_gap_bridge_t *bridge, uint32_t frame_ms) {
    if (bridge == NULL || frame_ms == 0u) {
        return 0u;
    }
    return (bridge->cfg.preroll_ms + frame_ms - 1u) / frame_ms;
}
