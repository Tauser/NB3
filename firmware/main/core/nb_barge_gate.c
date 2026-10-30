#include "nb_barge_gate.h"

static void close_gate(nb_barge_gate_t *g) {
    g->open = false;
    g->speech_run = 0;
    g->silence_run = 0;
}

void nb_barge_gate_init(nb_barge_gate_t *g, const nb_barge_gate_config_t *cfg) {
    g->cfg = *cfg;
    if (g->cfg.min_speech_frames < 1) {
        g->cfg.min_speech_frames = 1;
    }
    if (g->cfg.hold_frames < 1) {
        g->cfg.hold_frames = 1;
    }
    g->armed = false;
    close_gate(g);
}

void nb_barge_gate_arm(nb_barge_gate_t *g, bool armed) {
    g->armed = armed;
    close_gate(g);
}

nb_barge_action_t nb_barge_gate_step(nb_barge_gate_t *g, bool speech) {
    if (!g->armed) {
        return NB_BARGE_PASS;
    }
    if (g->open) {
        g->silence_run = speech ? 0 : (uint8_t)(g->silence_run + 1);
        if (g->silence_run >= g->cfg.hold_frames) {
            close_gate(g);
            return NB_BARGE_HOLD;
        }
        return NB_BARGE_PASS;
    }
    g->speech_run = speech ? (uint8_t)(g->speech_run + 1) : 0;
    if (g->speech_run >= g->cfg.min_speech_frames) {
        g->open = true;
        g->speech_run = 0;
        return NB_BARGE_OPENED;
    }
    return NB_BARGE_HOLD;
}
