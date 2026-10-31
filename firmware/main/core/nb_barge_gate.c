#include "nb_barge_gate.h"

#include <math.h>

static void close_gate(nb_barge_gate_t *g) {
    g->open = false;
    g->speech_run = 0;
    g->silence_run = 0;
}

void nb_barge_gate_init(nb_barge_gate_t *g, const nb_barge_gate_config_t *cfg) {
    g->cfg = *cfg;
    if (g->cfg.min_speech_frames < 1) {
        g->cfg.min_speech_frames = 1;
    } else if (g->cfg.min_speech_frames > NB_BARGE_MAX_RUN) {
        g->cfg.min_speech_frames = NB_BARGE_MAX_RUN;
    }
    if (g->cfg.hold_frames < 1) {
        g->cfg.hold_frames = 1;
    }
    if (g->cfg.hold_level_dbfs > g->cfg.min_level_dbfs) {
        g->cfg.hold_level_dbfs = g->cfg.min_level_dbfs;
    }
    g->armed = false;
    g->head = 0;
    close_gate(g);
}

void nb_barge_gate_arm(nb_barge_gate_t *g, bool armed) {
    g->armed = armed;
    close_gate(g);
}

int nb_barge_gate_run_average(const nb_barge_gate_t *g) {
    const uint8_t n = g->cfg.min_speech_frames;
    if (g->speech_run < n) {
        return NB_BARGE_LEVEL_SILENCE;
    }
    int sum = 0;
    for (uint8_t i = 0; i < n; i++) {
        sum += g->levels[(g->head + NB_BARGE_MAX_RUN - 1 - i) % NB_BARGE_MAX_RUN];
    }
    return sum / n;
}

nb_barge_action_t nb_barge_gate_step(nb_barge_gate_t *g, bool speech, int level_dbfs) {
    if (!g->armed) {
        return NB_BARGE_PASS;
    }
    if (g->open) {
        const bool sustaining_voice = speech && level_dbfs >= g->cfg.hold_level_dbfs;
        g->silence_run = sustaining_voice ? 0 : (uint8_t)(g->silence_run + 1);
        if (g->silence_run >= g->cfg.hold_frames) {
            close_gate(g);
            return NB_BARGE_HOLD;
        }
        return NB_BARGE_PASS;
    }
    if (!speech) {
        g->speech_run = 0;
        return NB_BARGE_HOLD;
    }
    g->levels[g->head] = level_dbfs;
    g->head = (uint8_t)((g->head + 1) % NB_BARGE_MAX_RUN);
    if (g->speech_run < g->cfg.min_speech_frames) {
        g->speech_run++;
    }
    if (nb_barge_gate_run_average(g) >= g->cfg.min_level_dbfs &&
        g->speech_run >= g->cfg.min_speech_frames) {
        g->open = true;
        g->speech_run = 0;
        return NB_BARGE_OPENED;
    }
    return NB_BARGE_HOLD;
}

int nb_barge_level_dbfs(const int16_t *samples, size_t count) {
    if (samples == NULL || count == 0) {
        return NB_BARGE_LEVEL_SILENCE;
    }
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += (double)samples[i] * (double)samples[i];
    }
    const double rms = sqrt(sum / (double)count) / 32768.0;
    if (rms <= 0.0) {
        return NB_BARGE_LEVEL_SILENCE;
    }
    const double db = floor(20.0 * log10(rms));
    return db < NB_BARGE_LEVEL_SILENCE ? NB_BARGE_LEVEL_SILENCE : (int)db;
}
