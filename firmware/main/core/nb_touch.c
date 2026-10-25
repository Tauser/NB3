#include "nb_touch.h"

#include <string.h>

nb_touch_config_t nb_touch_default_config(void) {
    nb_touch_config_t cfg = {
        .press_ratio = 0.03f,
        .release_ratio = 0.015f,
        .debounce = 3,
        .calib_samples = 10,
        .tap_max_ms = 600,
        .hold_ms = 1500,
        .drift_alpha = 1.0f / 512.0f,
    };
    return cfg;
}

void nb_touch_init(nb_touch_t *touch, const nb_touch_config_t *cfg) {
    memset(touch, 0, sizeof(*touch));
    touch->cfg = *cfg;
}

bool nb_touch_is_calibrated(const nb_touch_t *touch) {
    return touch->calib_count >= touch->cfg.calib_samples && touch->cfg.calib_samples > 0;
}

nb_touch_event_t nb_touch_update(nb_touch_t *touch, uint32_t raw, uint32_t now_ms) {
    if (!nb_touch_is_calibrated(touch)) {
        touch->calib_sum += raw;
        touch->calib_count++;
        if (nb_touch_is_calibrated(touch)) {
            touch->baseline = (float)touch->calib_sum / (float)touch->calib_count;
        }
        return NB_TOUCH_NONE;
    }

    const float value = (float)raw;
    const bool above_press = value > touch->baseline * (1.0f + touch->cfg.press_ratio);
    const bool below_release = value < touch->baseline * (1.0f + touch->cfg.release_ratio);

    if (!touch->touched) {
        if (above_press) {
            if (++touch->streak >= touch->cfg.debounce) {
                touch->touched = true;
                touch->hold_sent = false;
                touch->press_ms = now_ms;
                touch->streak = 0;
                return NB_TOUCH_PRESS;
            }
        } else {
            touch->streak = 0;
            // Follow slow drift only while clearly untouched.
            if (below_release) {
                touch->baseline += (value - touch->baseline) * touch->cfg.drift_alpha;
            }
        }
        return NB_TOUCH_NONE;
    }

    if (below_release) {
        if (++touch->streak >= touch->cfg.debounce) {
            touch->touched = false;
            touch->streak = 0;
            uint32_t held = now_ms - touch->press_ms;
            if (touch->hold_sent) {
                return NB_TOUCH_RELEASE;
            }
            return held <= touch->cfg.tap_max_ms ? NB_TOUCH_TAP : NB_TOUCH_RELEASE;
        }
        return NB_TOUCH_NONE;
    }

    touch->streak = 0;
    if (!touch->hold_sent && now_ms - touch->press_ms >= touch->cfg.hold_ms) {
        touch->hold_sent = true;
        return NB_TOUCH_HOLD;
    }
    return NB_TOUCH_NONE;
}
