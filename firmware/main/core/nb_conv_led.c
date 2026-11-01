#include "nb_conv_led.h"

void nb_conv_led_init(nb_conv_led_t *led, uint32_t hold_ms) {
    led->shown = NB_CONV_REST;
    led->pending = NB_CONV_REST;
    led->pending_since_ms = 0;
    led->hold_ms = hold_ms;
}

nb_conv_state_t nb_conv_led_step(nb_conv_led_t *led, nb_conv_state_t raw, uint32_t now_ms) {
    if (raw == led->shown) {
        led->pending = raw;
        return led->shown;
    }
    if (led->shown == NB_CONV_REST) {
        led->shown = raw;
        led->pending = raw;
        return led->shown;
    }
    if (raw != led->pending) {
        led->pending = raw;
        led->pending_since_ms = now_ms;
    }
    if (now_ms - led->pending_since_ms >= led->hold_ms) {
        led->shown = raw;
    }
    return led->shown;
}

// Green and blue follow the StackChan; the waiting color is provisional until
// it is confirmed on the board.
nb_rgb_t nb_conv_led_color(nb_conv_state_t state, uint8_t max_brightness) {
    uint8_t r = 0, g = 0, b = 0;
    switch (state) {
    case NB_CONV_LISTENING: g = 255; break;
    case NB_CONV_SPEAKING: g = 60; b = 255; break;
    case NB_CONV_WAITING: r = 255; g = 150; break;
    case NB_CONV_REST: break;
    }
    nb_rgb_t out = {
        (uint8_t)((r * max_brightness + 127) / 255),
        (uint8_t)((g * max_brightness + 127) / 255),
        (uint8_t)((b * max_brightness + 127) / 255),
    };
    return out;
}
