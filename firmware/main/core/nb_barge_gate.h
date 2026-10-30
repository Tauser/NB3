#ifndef NB_BARGE_GATE_H
#define NB_BARGE_GATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Uplink gate for interrupting the robot while it speaks. In realtime mode
// the server stops the reply as soon as it hears anything in the mic stream,
// so a knock or a fan is enough. While armed (the robot is speaking), frames
// are held back until the local voice detector has called `min_speech_frames`
// frames in a row speech; then the gate opens and the caller sends the frames
// it kept, so the server hears the start of the words. After `hold_frames`
// frames in a row without speech it closes again. Disarmed, every frame
// passes.

typedef struct {
    uint8_t min_speech_frames;
    uint8_t hold_frames;
} nb_barge_gate_config_t;

typedef struct {
    nb_barge_gate_config_t cfg;
    bool armed;
    bool open;
    uint8_t speech_run;
    uint8_t silence_run;
} nb_barge_gate_t;

typedef enum {
    NB_BARGE_PASS,    // send this frame
    NB_BARGE_HOLD,    // keep it back (the caller keeps the last few)
    NB_BARGE_OPENED,  // send the kept frames, then this one
} nb_barge_action_t;

void nb_barge_gate_init(nb_barge_gate_t *g, const nb_barge_gate_config_t *cfg);
// Arming or disarming always starts closed with no speech counted.
void nb_barge_gate_arm(nb_barge_gate_t *g, bool armed);
nb_barge_action_t nb_barge_gate_step(nb_barge_gate_t *g, bool speech);

#ifdef __cplusplus
}
#endif

#endif
