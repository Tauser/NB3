#ifndef NB_TURN_H
#define NB_TURN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where a conversation turn stands, from what the cloud really said: the
// speech-to-text result (the server has the question) and the first sentence
// of the answer. Speaking starts up to 50 ms after the text, before any audio,
// so neither the device state nor the voice detector says whether the robot is
// still waiting (docs/PLANO-FACE-E-COMPORTAMENTO, section 7). Pure core: time
// comes in as a parameter.
//
//   NONE      no turn in progress
//   WAITING   the text arrived, no sentence yet
//   ANSWERING the first sentence arrived: the robot is answering
//   STALLED   no sentence for NB_TURN_STALL_MS: the wait gives up showing
// Idle, or listening after a spoken answer, ends the turn.

#define NB_TURN_HAND_DELAY_MS 2500u  // the hand shows after this long without a sentence
#define NB_TURN_STALL_MS 20000u      // then "taking long" replaces it

typedef enum {
    NB_TURN_NONE = 0,
    NB_TURN_WAITING,
    NB_TURN_ANSWERING,
    NB_TURN_STALLED,
} nb_turn_state_t;

typedef enum {
    NB_TURN_DEVICE_OTHER = 0,
    NB_TURN_DEVICE_IDLE,
    NB_TURN_DEVICE_LISTENING,
    NB_TURN_DEVICE_SPEAKING,
} nb_turn_device_t;

typedef struct {
    nb_turn_state_t state;
    uint32_t text_id;  // identifies the text of this turn, to ignore a repeat
    uint32_t waiting_since_ms;
    nb_turn_device_t device;
    bool stall_reported;
} nb_turn_t;

typedef struct {
    bool wait_confirmed;  // show the hand: the server has the question, no answer yet
    bool speech_started;  // show the bubble: a sentence arrived or audio is audible
    bool stalled_now;     // true once, when the wait gives up
} nb_turn_out_t;

void nb_turn_init(nb_turn_t *turn);

// The speech-to-text result. `text_id` identifies its text (any hash); the same
// id again in a turn is a repeat. Ignored once the answer has begun.
void nb_turn_user_text(nb_turn_t *turn, uint32_t text_id, uint32_t now_ms);

// A sentence of the answer arrived.
void nb_turn_sentence(nb_turn_t *turn);

// The device state, every tick.
void nb_turn_device(nb_turn_t *turn, nb_turn_device_t device);

// Reads the turn. `audible`: the speaker is really playing.
void nb_turn_update(nb_turn_t *turn, bool audible, uint32_t now_ms, nb_turn_out_t *out);

// An error, a disconnection or a cancelled session: whatever was waited for is gone.
void nb_turn_reset(nb_turn_t *turn);

#ifdef __cplusplus
}
#endif

#endif  // NB_TURN_H
