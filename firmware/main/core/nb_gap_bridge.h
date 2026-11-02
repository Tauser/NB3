#ifndef NB_GAP_BRIDGE_H
#define NB_GAP_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hides short pauses from the server. The server decides that a turn is over
// when the uplink goes quiet for its own, fixed time, so a breath between two
// phrases cuts the question. Once the person has spoken, frames the voice
// detector calls silence are held back instead of sent, for at most max_hold_ms.
// If speech returns inside that time the caller sends the last preroll_ms it
// kept (the start of the word, which the detector only recognises late) and the
// server hears one continuous utterance. If the silence outlasts max_hold_ms the
// bridge gives up and sends live again, so the server still sees the end; the
// price is that a finished turn is answered max_hold_ms later.

typedef struct {
    uint32_t max_hold_ms;  // longest pause that is hidden; 0 turns the bridge off
    uint32_t preroll_ms;   // silence kept ahead of returning speech
} nb_gap_bridge_config_t;

typedef struct {
    nb_gap_bridge_config_t cfg;
    bool heard_speech;     // the person has spoken in this turn
    uint32_t silence_ms;   // length of the current run of silence
    bool holding;          // frames are being held
} nb_gap_bridge_t;

typedef enum {
    NB_GAP_SEND,            // send this frame; whatever was kept is dropped
    NB_GAP_HOLD,            // keep this frame in the preroll, do not send
    NB_GAP_FLUSH_AND_SEND,  // send the kept preroll, then this frame
} nb_gap_action_t;

void nb_gap_bridge_init(nb_gap_bridge_t *bridge, const nb_gap_bridge_config_t *cfg);

// A new turn: the person has not spoken yet and nothing is held.
void nb_gap_bridge_reset(nb_gap_bridge_t *bridge);

// One frame of frame_ms, tagged speech or silence by the voice detector.
nb_gap_action_t nb_gap_bridge_step(nb_gap_bridge_t *bridge, bool speech, uint32_t frame_ms);

// How many frames of frame_ms the caller keeps as preroll.
uint32_t nb_gap_bridge_preroll_frames(const nb_gap_bridge_t *bridge, uint32_t frame_ms);

#ifdef __cplusplus
}
#endif

#endif  // NB_GAP_BRIDGE_H
