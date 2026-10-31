#pragma once

// Shared by the gate and its downstream queue; changing preroll must also
// change the capacity that accepts its burst. This header is host-testable.
#define BARGE_IN_MIN_SPEECH_MS 300
#define BARGE_IN_HOLD_MS 1000
#define BARGE_IN_EXTRA_PREROLL_MS 180
#define BARGE_IN_MIN_LEVEL_DBFS (-24)
// Slight level hysteresis plus the hold time preserve weak syllables without
// letting indefinitely weak VAD-positive echo keep the gate open.
#define BARGE_IN_HOLD_LEVEL_DBFS (-25)
// Provisional acoustic tail, to be measured on the assembled enclosure.
#define BARGE_IN_OUTPUT_TAIL_MS 120

#define NB_AUDIO_FRAMES_CEIL(ms, frame_ms) (((ms) + (frame_ms) - 1) / (frame_ms))
#define NB_BARGE_PREROLL_FRAMES(frame_ms) \
    (NB_AUDIO_FRAMES_CEIL(BARGE_IN_MIN_SPEECH_MS, frame_ms) + \
     NB_AUDIO_FRAMES_CEIL(BARGE_IN_EXTRA_PREROLL_MS, frame_ms))
// Held frames + the opening frame + two live frames while the worker catches up.
#define NB_BARGE_ENCODE_QUEUE_FRAMES(frame_ms) (NB_BARGE_PREROLL_FRAMES(frame_ms) + 3)
