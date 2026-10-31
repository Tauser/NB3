#include "nb_spk_ref.h"

#include <stddef.h>
#include <string.h>

static bool is_power_of_two(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

void nb_spk_ref_init(nb_spk_ref_t *r, int16_t *history, uint32_t capacity, uint32_t delay) {
    memset(r, 0, sizeof(*r));
    if (history != NULL && is_power_of_two(capacity)) {
        r->history = history;
        r->capacity = capacity;
        memset(history, 0, capacity * sizeof(int16_t));
    }
    r->delay = delay;
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
    r->history_samples = n >= r->capacity - r->history_samples
                             ? r->capacity : r->history_samples + n;
    if (r->output_on) {
        r->played_since_on = true;
    }
}

void nb_spk_ref_captured(nb_spk_ref_t *r, uint32_t n) {
    r->captured += n;
    if (r->played_since_on) {
        const int32_t offset = (int32_t)(r->played - r->captured);
        if (!r->synced) {
            r->offset_min = r->offset_max = offset;
        } else {
            if (offset != r->offset) r->offset_changes++;
            if (offset < r->offset_min) r->offset_min = offset;
            if (offset > r->offset_max) r->offset_max = offset;
        }
        r->offset = offset;
        r->synced = true;
    }
}

void nb_spk_ref_dropped(nb_spk_ref_t *r, uint32_t n) {
    r->consumed += n;
    r->dropped_samples += n;
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
    // Select an older source sample: a positive delay shifts a source impulse
    // LATER in the returned stream. Unsigned arithmetic handles counter wrap.
    const uint32_t start = r->consumed + (uint32_t)r->offset - r->delay;
    const bool usable = r->output_on && r->synced && r->capacity != 0;
    const uint32_t mask = r->capacity - 1;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t index = start + i;
        const uint32_t age = r->played - index;  // 1 = the last sample played
        const bool available = usable && age >= 1 && age <= r->history_samples;
        out[i] = available ? r->history[index & mask] : 0;
        if (r->output_on && !available) r->unavailable_samples++;
    }
    r->consumed += n;
}
