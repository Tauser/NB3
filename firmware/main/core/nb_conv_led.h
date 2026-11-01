#ifndef NB_CONV_LED_H
#define NB_CONV_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_mood.h"

#ifdef __cplusplus
extern "C" {
#endif

// Conversation state on the body LEDs. It outranks the mood color while a
// conversation is on; at rest the LEDs go back to the mood. The raw input
// follows the device state, which can flip within a turn (the voice detector
// moves listening and waiting around about once a second), so a change
// between states only shows once it has held for hold_ms. Leaving rest is the
// exception: the wake word must answer at once.
typedef enum {
    NB_CONV_REST,       // no conversation: the LEDs show the mood
    NB_CONV_WAITING,    // connecting, or the robot is working out a reply
    NB_CONV_LISTENING,
    NB_CONV_SPEAKING,
} nb_conv_state_t;

#define NB_CONV_HOLD_MS 1500

typedef struct {
    nb_conv_state_t shown;
    nb_conv_state_t pending;  // raw state waiting to be committed
    uint32_t pending_since_ms;
    uint32_t hold_ms;
} nb_conv_led_t;

void nb_conv_led_init(nb_conv_led_t *led, uint32_t hold_ms);

// Feeds the raw state seen at now_ms and returns the state to show.
nb_conv_state_t nb_conv_led_step(nb_conv_led_t *led, nb_conv_state_t raw, uint32_t now_ms);

// Steady color of a conversation state, scaled to max_brightness (0..255).
// NB_CONV_REST has no color of its own: it returns black.
nb_rgb_t nb_conv_led_color(nb_conv_state_t state, uint8_t max_brightness);

#ifdef __cplusplus
}
#endif

#endif // NB_CONV_LED_H
