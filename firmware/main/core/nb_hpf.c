#include "nb_hpf.h"

#include <math.h>

#define PI_F 3.14159265358979323846f

void nb_hpf_reset(nb_hpf_t *hpf) {
    hpf->x1 = hpf->x2 = hpf->y1 = hpf->y2 = 0.0f;
}

void nb_hpf_init(nb_hpf_t *hpf, float sample_rate_hz, float cutoff_hz) {
    nb_hpf_reset(hpf);
    if (!(sample_rate_hz > 0.0f) || !(cutoff_hz > 0.0f) || cutoff_hz >= sample_rate_hz / 2.0f) {
        hpf->b0 = 1.0f;
        hpf->b1 = hpf->b2 = hpf->a1 = hpf->a2 = 0.0f;
        return;
    }
    // RBJ Audio EQ Cookbook high-pass with Q = 1/sqrt(2) (Butterworth).
    const float w0 = 2.0f * PI_F * cutoff_hz / sample_rate_hz;
    const float cos_w0 = cosf(w0);
    const float alpha = sinf(w0) / (2.0f * 0.70710678f);
    const float a0 = 1.0f + alpha;
    hpf->b0 = (1.0f + cos_w0) / 2.0f / a0;
    hpf->b1 = -(1.0f + cos_w0) / a0;
    hpf->b2 = (1.0f + cos_w0) / 2.0f / a0;
    hpf->a1 = -2.0f * cos_w0 / a0;
    hpf->a2 = (1.0f - alpha) / a0;
}

float nb_hpf_step(nb_hpf_t *hpf, float x) {
    const float y = hpf->b0 * x + hpf->b1 * hpf->x1 + hpf->b2 * hpf->x2 -
                    hpf->a1 * hpf->y1 - hpf->a2 * hpf->y2;
    hpf->x2 = hpf->x1;
    hpf->x1 = x;
    hpf->y2 = hpf->y1;
    hpf->y1 = y;
    return y;
}

void nb_hpf_process(nb_hpf_t *hpf, int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const float y = nb_hpf_step(hpf, samples[i]);
        samples[i] = y > 32767.0f ? 32767 : (y < -32767.0f ? -32767 : (int16_t)lrintf(y));
    }
}
