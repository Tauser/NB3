#include "nb_turn.h"

#include <string.h>

void nb_turn_init(nb_turn_t *turn)
{
    if (turn != NULL) {
        memset(turn, 0, sizeof(*turn));
    }
}

void nb_turn_reset(nb_turn_t *turn)
{
    if (turn != NULL) {
        turn->state = NB_TURN_NONE;
        turn->stall_reported = false;
    }
}

void nb_turn_user_text(nb_turn_t *turn, uint32_t text_id, uint32_t now_ms)
{
    if (turn == NULL) {
        return;
    }
    // Late (the answer already began or the wait gave up) or repeated: not a new wait.
    if (turn->state == NB_TURN_ANSWERING || turn->state == NB_TURN_STALLED ||
        (turn->state == NB_TURN_WAITING && turn->text_id == text_id)) {
        return;
    }
    turn->state = NB_TURN_WAITING;
    turn->text_id = text_id;
    turn->waiting_since_ms = now_ms;
    turn->stall_reported = false;
}

void nb_turn_sentence(nb_turn_t *turn)
{
    if (turn == NULL) {
        return;
    }
    // A sentence with no text first (a greeting, an alert) is still an answer.
    turn->state = NB_TURN_ANSWERING;
}

void nb_turn_device(nb_turn_t *turn, nb_turn_device_t device)
{
    if (turn == NULL) {
        return;
    }
    const nb_turn_device_t before = turn->device;
    turn->device = device;
    if (device == NB_TURN_DEVICE_IDLE ||
        (device == NB_TURN_DEVICE_LISTENING && before == NB_TURN_DEVICE_SPEAKING)) {
        nb_turn_reset(turn);
    } else if (device == NB_TURN_DEVICE_OTHER && before != NB_TURN_DEVICE_OTHER) {
        // Connecting, upgrading, an error: the turn does not survive it.
        nb_turn_reset(turn);
    }
}

void nb_turn_update(nb_turn_t *turn, bool audible, uint32_t now_ms, nb_turn_out_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (turn == NULL) {
        return;
    }
    if (turn->state == NB_TURN_WAITING) {
        const uint32_t waited = now_ms - turn->waiting_since_ms;
        if (waited >= NB_TURN_STALL_MS) {
            turn->state = NB_TURN_STALLED;
            out->stalled_now = !turn->stall_reported;
            turn->stall_reported = true;
        } else {
            out->wait_confirmed = waited >= NB_TURN_HAND_DELAY_MS;
        }
    }
    out->speech_started = turn->state == NB_TURN_ANSWERING ||
                          (turn->device == NB_TURN_DEVICE_SPEAKING && audible);
}
