#include "nb_output_activity.h"

void nb_output_activity_written(nb_output_activity_t *s, uint32_t now_ms) {
    s->written = true;
    s->write_ms = now_ms;
}

void nb_output_activity_played(nb_output_activity_t *s, uint32_t now_ms) {
    s->played = true;
    s->signal_ms = now_ms;
}

bool nb_output_activity_active(const nb_output_activity_t *s, uint32_t now_ms,
                               uint32_t dma_ms, uint32_t tail_ms) {
    return (s->written && (uint32_t)(now_ms - s->write_ms) < dma_ms + tail_ms) ||
           (s->played && (uint32_t)(now_ms - s->signal_ms) < tail_ms);
}
