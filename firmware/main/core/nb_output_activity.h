#ifndef NB_OUTPUT_ACTIVITY_H
#define NB_OUTPUT_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Caller serializes the output task, DMA callback and input task. Clock is
// injected; unsigned elapsed time handles millisecond counter wrap.
typedef struct {
    bool written;
    bool played;
    uint32_t write_ms;
    uint32_t signal_ms;
} nb_output_activity_t;

void nb_output_activity_written(nb_output_activity_t *s, uint32_t now_ms);
void nb_output_activity_played(nb_output_activity_t *s, uint32_t now_ms);
bool nb_output_activity_active(const nb_output_activity_t *s, uint32_t now_ms,
                               uint32_t dma_ms, uint32_t tail_ms);

#ifdef __cplusplus
}
#endif
#endif
