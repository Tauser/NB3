#ifndef NB_BARGE_GATE_H
#define NB_BARGE_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Uplink gate for interrupting the robot while it speaks. In realtime mode
// the server stops the reply as soon as it hears anything in the mic stream,
// so a knock, a voice across the room or the echo the AEC leaves behind is
// enough. While armed (the robot is speaking), frames are held back until
// the voice detector has called the last `min_speech_frames` frames speech
// and their average level reaches `min_level_dbfs`: someone talking over the
// robot up close is louder than both the residual echo and far-away voices,
// and averaging rides over the dips between syllables. Then the gate opens
// and the caller sends the frames it kept, so the server hears the start of
// the words. After `hold_frames` frames without speech above hold_level_dbfs it closes
// again. Disarmed, every frame passes.

#define NB_BARGE_MAX_RUN 16
#define NB_BARGE_LEVEL_SILENCE (-100)

typedef struct {
    uint8_t min_speech_frames;  // 1..NB_BARGE_MAX_RUN
    uint8_t hold_frames;
    int min_level_dbfs;
    int hold_level_dbfs;       // <= min_level_dbfs, level hysteresis while open
} nb_barge_gate_config_t;

typedef struct {
    nb_barge_gate_config_t cfg;
    bool armed;
    bool open;
    uint8_t speech_run;  // consecutive speech frames, capped at min_speech_frames
    uint8_t silence_run;
    uint8_t head;
    int levels[NB_BARGE_MAX_RUN];  // dBFS of the latest speech frames
} nb_barge_gate_t;

typedef enum {
    NB_BARGE_PASS,    // send this frame
    NB_BARGE_HOLD,    // keep it back (the caller keeps the last few)
    NB_BARGE_OPENED,  // send the kept frames, then this one
} nb_barge_action_t;

void nb_barge_gate_init(nb_barge_gate_t *g, const nb_barge_gate_config_t *cfg);
// Arming or disarming always starts closed with no speech counted.
void nb_barge_gate_arm(nb_barge_gate_t *g, bool armed);
nb_barge_action_t nb_barge_gate_step(nb_barge_gate_t *g, bool speech, int level_dbfs);
// Average level of the last min_speech_frames frames while they are all
// speech, else NB_BARGE_LEVEL_SILENCE. Tells how close a held run came.
int nb_barge_gate_run_average(const nb_barge_gate_t *g);

// RMS level of a block of samples in whole dBFS, rounded down; silence (or
// no samples) gives NB_BARGE_LEVEL_SILENCE.
int nb_barge_level_dbfs(const int16_t *samples, size_t count);

#ifdef __cplusplus
}
#endif

#endif
