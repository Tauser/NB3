#ifndef NB_SPK_REF_H
#define NB_SPK_REF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Speaker reference for echo cancellation on a board with no codec loopback.
// Mic and amp share one duplex I2S bus, so both run on the same clock: the
// TX DMA reports each block right after it is played, the RX DMA each block
// right after it is captured. Counting both gives, at every capture, how
// far the speaker timeline is ahead of the mic timeline; each mic sample
// handed to the reader then gets the speaker sample played at the same
// moment, taken from a history of what was actually played.
//
// Positive `delay` selects older speaker samples: ref[n] = speaker[n-delay]
// when offset is zero. This DELAYS the reference waveform. It must be
// calibrated against the actual mic/ref lag, not treated as a causal lead.
//
// Not thread-safe: the caller serializes calls (they come from two ISRs and
// the reader task).

typedef struct {
    int16_t *history;    // last `capacity` played samples, indexed by played count
    uint32_t capacity;   // power of two
    uint32_t history_samples;  // valid samples, capped at capacity
    uint32_t delay;
    uint32_t played;     // samples played since init (wraps)
    uint32_t captured;   // samples captured by the RX DMA since init (wraps)
    uint32_t consumed;   // mic samples handed to the reader since init (wraps)
    int32_t offset;      // played - captured at the last capture
    bool output_on;
    bool played_since_on;
    bool synced;         // offset measured since the output last came on
    // Cumulative diagnostics, wrapping naturally; no logging in the ISR.
    uint32_t dropped_samples;
    uint32_t unavailable_samples;
    uint32_t offset_changes;
    int32_t offset_min;
    int32_t offset_max;
} nb_spk_ref_t;

// capacity must be a power of two; anything else turns the reference off
// (reads give silence).
void nb_spk_ref_init(nb_spk_ref_t *r, int16_t *history, uint32_t capacity, uint32_t delay);

// TX DMA finished playing n samples.
void nb_spk_ref_played(nb_spk_ref_t *r, const int16_t *samples, uint32_t n);

// RX DMA finished capturing n samples.
void nb_spk_ref_captured(nb_spk_ref_t *r, uint32_t n);

// The RX queue overflowed and n captured samples will never be read.
void nb_spk_ref_dropped(nb_spk_ref_t *r, uint32_t n);

// The RX channel was (re)enabled, which empties its queue: the next sample
// read is the next one captured.
void nb_spk_ref_input_restarted(nb_spk_ref_t *r);

// Speaker output enabled or disabled. While off, or until the timelines are
// measured again after it comes on, the reference is silence.
void nb_spk_ref_set_output(nb_spk_ref_t *r, bool on);

// Reference for the next n mic samples the reader got, in order.
void nb_spk_ref_read(nb_spk_ref_t *r, int16_t *out, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif // NB_SPK_REF_H
