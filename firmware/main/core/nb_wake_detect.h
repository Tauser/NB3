#ifndef NB_WAKE_DETECT_H
#define NB_WAKE_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decision side of the microWakeWord detector. The streaming model gives one
// probability per inference (every 30 ms); a single high value is often a
// blip, so the robot wakes only when the average of the last `window`
// probabilities passes the cutoff, the same rule microWakeWord's ROC was
// measured with. Right after a reset the model's internal state still holds
// silence, so the first `warmup_steps` inferences are not trusted.
//
// Averages that rise above `report_floor` but never reach the cutoff are
// reported once, with their peak, when they fall back: those near misses are
// what a cutoff gets tuned with on the real robot.

#define NB_WAKE_DETECT_MAX_WINDOW 16

typedef struct {
    uint8_t cutoff;         // 0-255 probability scale; wakes when average > cutoff
    uint8_t window;         // 1..NB_WAKE_DETECT_MAX_WINDOW probabilities averaged
    uint16_t warmup_steps;  // inferences ignored after init/reset
    uint8_t report_floor;   // averages above this and <= cutoff are near misses
} nb_wake_detect_config_t;

typedef enum {
    NB_WAKE_NONE = 0,
    NB_WAKE_DETECTED,
    NB_WAKE_NEAR_MISS,
} nb_wake_event_t;

typedef struct {
    nb_wake_detect_config_t cfg;
    uint8_t probs[NB_WAKE_DETECT_MAX_WINDOW];
    uint8_t head;
    uint8_t count;
    uint16_t warmup_left;
    uint8_t peak;  // highest average of the current near-miss episode, 0 if none
} nb_wake_detect_t;

// An out-of-range window is clamped to 1..NB_WAKE_DETECT_MAX_WINDOW.
void nb_wake_detect_init(nb_wake_detect_t *d, const nb_wake_detect_config_t *cfg);

// Forgets the window and any near miss in progress, and starts the warm-up.
void nb_wake_detect_reset(nb_wake_detect_t *d);

// Feeds one model probability. On NB_WAKE_DETECTED the detector resets itself
// and *peak gets the average that fired; on NB_WAKE_NEAR_MISS *peak gets the
// highest average of the episode. peak may be NULL.
nb_wake_event_t nb_wake_detect_step(nb_wake_detect_t *d, uint8_t probability, uint8_t *peak);

// Current window average (0 until the first probability after the warm-up).
uint8_t nb_wake_detect_average(const nb_wake_detect_t *d);

// Maps one raw microfrontend feature to the model's int8 input. Training saw
// the features as raw * 0.0390625 (pymicro-features' float scale); the
// model's input tensor then quantizes that with its own scale and zero point.
int8_t nb_wake_quantize_feature(uint16_t raw, float input_scale, int input_zero_point);

#ifdef __cplusplus
}
#endif

#endif // NB_WAKE_DETECT_H
