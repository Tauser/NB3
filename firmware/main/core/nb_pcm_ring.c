#include "nb_pcm_ring.h"

void nb_pcm_ring_init(nb_pcm_ring_t *ring, int16_t *storage, size_t capacity) {
    ring->storage = storage;
    ring->capacity = storage ? capacity : 0;
    ring->head = 0;
    ring->count = 0;
}

void nb_pcm_ring_clear(nb_pcm_ring_t *ring) {
    ring->head = 0;
    ring->count = 0;
}

void nb_pcm_ring_push(nb_pcm_ring_t *ring, const int16_t *samples, size_t count) {
    if (ring->capacity == 0 || count == 0) {
        return;
    }
    // Only the newest `capacity` samples of an oversized push can survive.
    if (count > ring->capacity) {
        samples += count - ring->capacity;
        count = ring->capacity;
    }
    for (size_t i = 0; i < count; i++) {
        size_t tail = (ring->head + ring->count) % ring->capacity;
        ring->storage[tail] = samples[i];
        if (ring->count == ring->capacity) {
            ring->head = (ring->head + 1) % ring->capacity;
        } else {
            ring->count++;
        }
    }
}

size_t nb_pcm_ring_count(const nb_pcm_ring_t *ring) {
    return ring->count;
}

size_t nb_pcm_ring_pop(nb_pcm_ring_t *ring, int16_t *out, size_t max) {
    size_t n = ring->count < max ? ring->count : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = ring->storage[ring->head];
        ring->head = (ring->head + 1) % ring->capacity;
    }
    ring->count -= n;
    return n;
}
