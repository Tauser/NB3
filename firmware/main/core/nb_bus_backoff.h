#ifndef NB_BUS_BACKOFF_H
#define NB_BUS_BACKOFF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Paces the servo bus while nobody answers on it. After `silent_cycles`
// cycles in a row without a single reply the bus is only tried once per
// `probe_period_ms`; the first reply brings the normal rate back. It decides
// how often to try, never whether a move is safe: the caller keeps the full
// rate whenever the motion is armed. Clock is injected; the signed difference
// handles millisecond counter wrap.

typedef struct {
    uint8_t silent_cycles;     // cycles without any reply before backing off, >= 1
    uint32_t probe_period_ms;  // spacing of the tries while backed off
} nb_bus_backoff_cfg_t;

typedef struct {
    nb_bus_backoff_cfg_t cfg;
    uint8_t silent;
    bool backed_off;
    uint32_t next_probe_ms;
} nb_bus_backoff_t;

typedef enum {
    NB_BUS_BACKOFF_NO_CHANGE,
    NB_BUS_BACKOFF_ENTERED,
    NB_BUS_BACKOFF_LEFT
} nb_bus_backoff_change_t;

void nb_bus_backoff_init(nb_bus_backoff_t *b, const nb_bus_backoff_cfg_t *cfg);
// True when the bus may be used this cycle.
bool nb_bus_backoff_due(const nb_bus_backoff_t *b, uint32_t now_ms);
// After each cycle that used the bus: whether at least one servo replied.
nb_bus_backoff_change_t nb_bus_backoff_report(nb_bus_backoff_t *b, bool answered,
                                              uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
