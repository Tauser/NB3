#include "nb_spk_ref.h"

#include <stddef.h>
#include <string.h>

static bool is_power_of_two(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

void nb_spk_ref_init(nb_spk_ref_t *r, int16_t *history, uint32_t capacity, uint32_t lead) {
    memset(r, 0, sizeof(*r));
    if (history != NULL && is_power_of_two(capacity)) {
        r->history = history;
        r->capacity = capacity;
        memset(history, 0, capacity * sizeof(int16_t));
    }
    r->lead = lead;
}

void nb_spk_ref_played(nb_spk_ref_t *r, const int16_t *samples, uint32_t n) {
    if (r->capacity == 0) {
        return;
    }
    const uint32_t mask = r->capacity - 1;
    for (uint32_t i = 0; i < n; i++) {
        r->history[(r->played + i) & mask] = samples[i];
    }
    r->played += n;
    if (r->output_on) {
        r->played_since_on = true;
    }
}

void nb_spk_ref_captured(nb_spk_ref_t *r, uint32_t n) {
    r->captured += n;
    if (r->played_since_on) {
        r->offset = (int32_t)(r->played - r->captured);
        r->synced = true;
    }
}

void nb_spk_ref_dropped(nb_spk_ref_t *r, uint32_t n) {
    r->consumed += n;
}

void nb_spk_ref_input_restarted(nb_spk_ref_t *r) {
    r->consumed = r->captured;
}

void nb_spk_ref_set_output(nb_spk_ref_t *r, bool on) {
    r->output_on = on;
    r->played_since_on = false;
    r->synced = false;
}

void nb_spk_ref_read(nb_spk_ref_t *r, int16_t *out, uint32_t n) {
    // Speaker sample played when mic sample `consumed` was captured, moved
    // `lead` earlier. Unsigned arithmetic keeps this right across wrap.
    const uint32_t start = r->consumed + (uint32_t)r->offset - r->lead;
    const bool usable = r->output_on && r->synced && r->capacity != 0;
    const uint32_t mask = r->capacity - 1;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t index = start + i;
        const uint32_t age = r->played - index;  // 1 = the last sample played
        out[i] = usable && age >= 1 && age <= r->capacity ? r->history[index & mask] : 0;
    }
    r->consumed += n;
}
