#ifndef NB_HPF_H
#define NB_HPF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Second-order Butterworth high-pass for the microphone. The NB3 mic signal
// is dominated by energy below ~150 Hz (fan and TV rumble, plus low-frequency
// noise from the mic itself) while speech lives at 300-3400 Hz; cutting the
// low band lowers the noise floor and frees headroom that was clipping,
// without touching the voice.
typedef struct {
    float b0, b1, b2, a1, a2;  // normalized biquad coefficients (a0 = 1)
    float x1, x2, y1, y2;      // direct form I history
} nb_hpf_t;

// cutoff_hz must be below sample_rate_hz / 2; out-of-range input leaves the
// filter as a pass-through.
void nb_hpf_init(nb_hpf_t *hpf, float sample_rate_hz, float cutoff_hz);

void nb_hpf_reset(nb_hpf_t *hpf);

// Filters one sample. Unbounded input, so a caller can filter before it
// saturates to its output format and keep the headroom the low band used.
float nb_hpf_step(nb_hpf_t *hpf, float x);

// Filters `count` samples in place, saturating to the int16 range.
void nb_hpf_process(nb_hpf_t *hpf, int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif

#endif // NB_HPF_H
