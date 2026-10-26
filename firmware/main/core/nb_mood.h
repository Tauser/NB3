#ifndef NB_MOOD_H
#define NB_MOOD_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mood lighting for the body LEDs: each expression has a color and a
// brightness envelope (steady, breathing or pulsing; DIZZY cycles hues).
// Color changes ease in, and the output never exceeds max_brightness, which
// keeps the LEDs from glaring next to the face or drawing too much current.
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} nb_rgb_t;

typedef struct {
    nb_expression_t expression;
    float r, g, b;           // eased base color, 0..255
    uint8_t max_brightness;  // 0..255 scale on the final output
    uint32_t last_ms;
} nb_mood_t;

#define NB_MOOD_EASE_MS 250.0f

void nb_mood_init(nb_mood_t *mood, uint8_t max_brightness, uint32_t now_ms);

void nb_mood_set_expression(nb_mood_t *mood, nb_expression_t expression);

// Advances to now_ms and returns the color to show on every LED.
nb_rgb_t nb_mood_step(nb_mood_t *mood, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // NB_MOOD_H
