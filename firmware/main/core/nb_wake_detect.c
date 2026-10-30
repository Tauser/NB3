#include "nb_wake_detect.h"

#include <math.h>
#include <stddef.h>

#define FEATURE_FLOAT_SCALE 0.0390625f

void nb_wake_detect_reset(nb_wake_detect_t *d) {
    d->head = 0;
    d->count = 0;
    d->peak = 0;
    d->warmup_left = d->cfg.warmup_steps;
}

void nb_wake_detect_init(nb_wake_detect_t *d, const nb_wake_detect_config_t *cfg) {
    d->cfg = *cfg;
    if (d->cfg.window < 1) {
        d->cfg.window = 1;
    } else if (d->cfg.window > NB_WAKE_DETECT_MAX_WINDOW) {
        d->cfg.window = NB_WAKE_DETECT_MAX_WINDOW;
    }
    nb_wake_detect_reset(d);
}

uint8_t nb_wake_detect_average(const nb_wake_detect_t *d) {
    if (d->count == 0) {
        return 0;
    }
    unsigned sum = 0;
    for (uint8_t i = 0; i < d->count; i++) {
        sum += d->probs[i];
    }
    return (uint8_t)(sum / d->count);
}

nb_wake_event_t nb_wake_detect_step(nb_wake_detect_t *d, uint8_t probability, uint8_t *peak) {
    if (d->warmup_left > 0) {
        d->warmup_left--;
        return NB_WAKE_NONE;
    }

    d->probs[d->head] = probability;
    d->head = (uint8_t)((d->head + 1) % d->cfg.window);
    if (d->count < d->cfg.window) {
        d->count++;
    }

    const uint8_t avg = nb_wake_detect_average(d);
    // A partial window would let one blip after the warm-up fire on its own.
    if (d->count == d->cfg.window && avg > d->cfg.cutoff) {
        if (peak != NULL) {
            *peak = avg;
        }
        nb_wake_detect_reset(d);
        return NB_WAKE_DETECTED;
    }

    if (avg > d->cfg.report_floor) {
        if (avg > d->peak) {
            d->peak = avg;
        }
        return NB_WAKE_NONE;
    }
    if (d->peak > 0) {
        if (peak != NULL) {
            *peak = d->peak;
        }
        d->peak = 0;
        return NB_WAKE_NEAR_MISS;
    }
    return NB_WAKE_NONE;
}

static int8_t clamp_int8(long v) {
    return (int8_t)(v < -128 ? -128 : (v > 127 ? 127 : v));
}

int8_t nb_wake_quantize_feature(uint16_t raw, float input_scale, int input_zero_point) {
    if (!(input_scale > 0.0f)) {
        return clamp_int8(input_zero_point);
    }
    return clamp_int8(lrintf((float)raw * FEATURE_FLOAT_SCALE / input_scale) + input_zero_point);
}

static char lower_ascii(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

bool nb_wake_text_mentions(const char *text, const char *word) {
    if (text == NULL || word == NULL || word[0] == '\0') {
        return false;
    }
    for (const char *start = text; *start != '\0'; start++) {
        size_t i = 0;
        while (word[i] != '\0' && start[i] != '\0' && lower_ascii(start[i]) == lower_ascii(word[i])) {
            i++;
        }
        if (word[i] == '\0') {
            return true;
        }
    }
    return false;
}
