#include <math.h>
#include <stdlib.h>

#include "nb_hpf.h"
#include "nb_test.h"

#define FS 24000.0f
#define CUTOFF 150.0f
#define AMPLITUDE 10000.0f
#define N 24000  // one second

static int16_t buf[N];

// Gain in dB of a steady sine through a fresh filter, measured on the second
// half so the start-up transient is gone.
static float gain_db(float freq_hz, float fs, float cutoff) {
    nb_hpf_t hpf;
    nb_hpf_init(&hpf, fs, cutoff);
    for (int i = 0; i < N; i++) {
        buf[i] = (int16_t)lrintf(AMPLITUDE * sinf(2.0f * 3.14159265f * freq_hz * (float)i / fs));
    }
    nb_hpf_process(&hpf, buf, N);
    double sum = 0;
    for (int i = N / 2; i < N; i++) {
        sum += (double)buf[i] * buf[i];
    }
    double rms = sqrt(sum / (N / 2));
    return 20.0f * (float)log10(rms / (AMPLITUDE / sqrt(2.0)));
}

static void test_rumble_is_cut(void) {
    NB_CHECK(gain_db(50.0f, FS, CUTOFF) < -15.0f);
    NB_CHECK(gain_db(80.0f, FS, CUTOFF) < -9.0f);
}

static void test_cutoff_is_minus_three_db(void) {
    float g = gain_db(CUTOFF, FS, CUTOFF);
    NB_CHECK(g < -2.5f && g > -3.5f);
}

static void test_speech_band_passes(void) {
    NB_CHECK(gain_db(300.0f, FS, CUTOFF) > -1.0f);
    NB_CHECK(fabsf(gain_db(1000.0f, FS, CUTOFF)) < 0.2f);
    NB_CHECK(fabsf(gain_db(3400.0f, FS, CUTOFF)) < 0.2f);
}

static void test_dc_is_removed(void) {
    nb_hpf_t hpf;
    nb_hpf_init(&hpf, FS, CUTOFF);
    for (int i = 0; i < N; i++) {
        buf[i] = 8000;
    }
    nb_hpf_process(&hpf, buf, N);
    NB_CHECK(abs(buf[N - 1]) <= 1);
}

static void test_saturates_instead_of_wrapping(void) {
    nb_hpf_t hpf;
    nb_hpf_init(&hpf, FS, CUTOFF);
    // A full-scale step overshoots; the output must clip, not wrap around.
    for (int i = 0; i < 100; i++) {
        buf[i] = i < 50 ? -32767 : 32767;
    }
    nb_hpf_process(&hpf, buf, 100);
    NB_CHECK_EQ_INT(32767, buf[50]);
}

static void test_state_carries_across_calls(void) {
    nb_hpf_t a, b;
    nb_hpf_init(&a, FS, CUTOFF);
    nb_hpf_init(&b, FS, CUTOFF);
    int16_t whole[480], split[480];
    for (int i = 0; i < 480; i++) {
        whole[i] = split[i] = (int16_t)lrintf(AMPLITUDE * sinf(0.05f * (float)i));
    }
    nb_hpf_process(&a, whole, 480);
    nb_hpf_process(&b, split, 240);
    nb_hpf_process(&b, split + 240, 240);
    int same = 1;
    for (int i = 0; i < 480; i++) {
        same &= whole[i] == split[i];
    }
    NB_CHECK(same);
}

static void test_step_keeps_headroom_beyond_int16(void) {
    // Low-frequency content far above int16 full scale plus a small 1 kHz
    // tone: filtering before saturating leaves only the tone, unclipped.
    nb_hpf_t hpf;
    nb_hpf_init(&hpf, FS, CUTOFF);
    float peak = 0;
    for (int i = 0; i < N; i++) {
        float t = (float)i / FS;
        float x = 200000.0f * sinf(2.0f * 3.14159265f * 30.0f * t) +
                  5000.0f * sinf(2.0f * 3.14159265f * 1000.0f * t);
        float y = nb_hpf_step(&hpf, x);
        if (i > N / 2 && fabsf(y) > peak) {
            peak = fabsf(y);
        }
    }
    NB_CHECK(peak < 32767.0f);
    NB_CHECK(peak > 4500.0f);
}

static void test_invalid_config_passes_through(void) {
    nb_hpf_t hpf;
    nb_hpf_init(&hpf, FS, FS);  // cutoff above Nyquist
    int16_t s[3] = {123, -456, 789};
    nb_hpf_process(&hpf, s, 3);
    NB_CHECK(s[0] == 123 && s[1] == -456 && s[2] == 789);
}

int main(void) {
    NB_RUN(test_rumble_is_cut);
    NB_RUN(test_cutoff_is_minus_three_db);
    NB_RUN(test_speech_band_passes);
    NB_RUN(test_dc_is_removed);
    NB_RUN(test_saturates_instead_of_wrapping);
    NB_RUN(test_state_carries_across_calls);
    NB_RUN(test_step_keeps_headroom_beyond_int16);
    NB_RUN(test_invalid_config_passes_through);
    NB_TEST_MAIN_END();
}
