#ifndef NB_PCM_RING_H
#define NB_PCM_RING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-capacity PCM ring that keeps the newest samples. It holds the
// microphone audio captured between wake-word detection and the start of
// voice processing, so the start of the user's sentence is replayed instead
// of lost. Storage is injected (PSRAM on the device); the ring never
// allocates.
typedef struct {
    int16_t *storage;
    size_t capacity;
    size_t head;   // index of the oldest sample
    size_t count;
} nb_pcm_ring_t;

// A NULL storage or zero capacity leaves the ring permanently empty.
void nb_pcm_ring_init(nb_pcm_ring_t *ring, int16_t *storage, size_t capacity);

void nb_pcm_ring_clear(nb_pcm_ring_t *ring);

// Appends samples; when full, the oldest ones are overwritten.
void nb_pcm_ring_push(nb_pcm_ring_t *ring, const int16_t *samples, size_t count);

size_t nb_pcm_ring_count(const nb_pcm_ring_t *ring);

// Removes up to `max` of the oldest samples into `out`; returns how many.
size_t nb_pcm_ring_pop(nb_pcm_ring_t *ring, int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif // NB_PCM_RING_H
