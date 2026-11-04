#include "nb_led_settings.h"

#include <stddef.h>

void nb_led_settings_default(nb_led_settings_t *s, uint8_t brightness)
{
    if (s == NULL) {
        return;
    }
    s->on = true;
    s->brightness = brightness;
    s->fixed_color = false;
    s->color.r = s->color.g = s->color.b = 255;
    nb_led_settings_clamp(s);
}

bool nb_led_settings_clamp(nb_led_settings_t *s)
{
    if (s == NULL) {
        return false;
    }
    bool changed = false;
    if (s->brightness > NB_LED_BRIGHTNESS_MAX) {
        s->brightness = NB_LED_BRIGHTNESS_MAX;
        changed = true;
    } else if (s->brightness < NB_LED_BRIGHTNESS_MIN) {
        s->brightness = NB_LED_BRIGHTNESS_MIN;
        changed = true;
    }
    return changed;
}

nb_rgb_t nb_led_settings_resolve(const nb_led_settings_t *s, nb_rgb_t mood_or_conversation)
{
    const nb_rgb_t black = {0, 0, 0};
    if (s == NULL || !s->on) {
        return black;
    }
    if (!s->fixed_color) {
        return mood_or_conversation;
    }
    // The strongest channel gets the brightness; the others keep their share.
    uint8_t peak = s->color.r;
    if (s->color.g > peak) {
        peak = s->color.g;
    }
    if (s->color.b > peak) {
        peak = s->color.b;
    }
    if (peak == 0) {
        return black;
    }
    nb_rgb_t out;
    out.r = (uint8_t)((uint32_t)s->color.r * s->brightness / peak);
    out.g = (uint8_t)((uint32_t)s->color.g * s->brightness / peak);
    out.b = (uint8_t)((uint32_t)s->color.b * s->brightness / peak);
    return out;
}
