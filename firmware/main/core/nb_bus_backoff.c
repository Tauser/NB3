#include "nb_bus_backoff.h"

void nb_bus_backoff_init(nb_bus_backoff_t *b, const nb_bus_backoff_cfg_t *cfg) {
    b->cfg = *cfg;
    if (b->cfg.silent_cycles < 1) {
        b->cfg.silent_cycles = 1;
    }
    b->silent = 0;
    b->backed_off = false;
    b->next_probe_ms = 0;
}

bool nb_bus_backoff_due(const nb_bus_backoff_t *b, uint32_t now_ms) {
    return !b->backed_off || (int32_t)(now_ms - b->next_probe_ms) >= 0;
}

nb_bus_backoff_change_t nb_bus_backoff_report(nb_bus_backoff_t *b, bool answered,
                                              uint32_t now_ms) {
    if (answered) {
        const bool was = b->backed_off;
        b->silent = 0;
        b->backed_off = false;
        return was ? NB_BUS_BACKOFF_LEFT : NB_BUS_BACKOFF_NO_CHANGE;
    }
    if (b->silent < UINT8_MAX) {
        b->silent++;
    }
    if (b->silent < b->cfg.silent_cycles) {
        return NB_BUS_BACKOFF_NO_CHANGE;
    }
    const bool entered = !b->backed_off;
    b->backed_off = true;
    b->next_probe_ms = now_ms + b->cfg.probe_period_ms;
    return entered ? NB_BUS_BACKOFF_ENTERED : NB_BUS_BACKOFF_NO_CHANGE;
}
