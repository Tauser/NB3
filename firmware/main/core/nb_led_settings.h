#ifndef NB_LED_SETTINGS_H
#define NB_LED_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_mood.h"

#ifdef __cplusplus
extern "C" {
#endif

// What the owner may set about the body LEDs: on or off, how bright and, if
// wanted, one fixed color instead of the mood's. The brightness has a hard
// ceiling here, so no setting can make the two LEDs glare or draw too much
// current. Pure core: the strip belongs to behavior/mood_lights.

#define NB_LED_BRIGHTNESS_MAX 96u  // of 255: about 38%, two WS2812 stay near 45 mA
#define NB_LED_BRIGHTNESS_MIN 1u

typedef struct {
    bool on;
    uint8_t brightness;  // NB_LED_BRIGHTNESS_MIN..NB_LED_BRIGHTNESS_MAX
    bool fixed_color;    // false: the mood's color
    nb_rgb_t color;      // used when fixed_color
} nb_led_settings_t;

// On, the given brightness, the mood's color.
void nb_led_settings_default(nb_led_settings_t *s, uint8_t brightness);

// Pulls every field into its range; returns true if anything had to change.
bool nb_led_settings_clamp(nb_led_settings_t *s);

// The color to show: black when off; otherwise the fixed color scaled to the
// brightness when one is set, else the mood's (or the conversation's) color,
// which already carries the brightness.
nb_rgb_t nb_led_settings_resolve(const nb_led_settings_t *s, nb_rgb_t mood_or_conversation);

#ifdef __cplusplus
}
#endif

#endif  // NB_LED_SETTINGS_H
