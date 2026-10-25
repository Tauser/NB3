#ifndef NB_TOUCH_H
#define NB_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Body-touch detection on a capacitive pad. On the ESP32-S3 the raw reading
// rises when touched; detection is relative to a baseline that is calibrated
// at start and then follows slow drift (humidity, temperature) while the pad
// is untouched, so a fixed threshold never goes stale.
typedef enum {
    NB_TOUCH_NONE = 0,
    NB_TOUCH_PRESS,    // contact started
    NB_TOUCH_TAP,      // released quickly
    NB_TOUCH_HOLD,     // held long enough to count as petting (fires once)
    NB_TOUCH_RELEASE,  // contact ended after a hold
} nb_touch_event_t;

typedef struct {
    float press_ratio;        // touched above baseline * (1 + press_ratio)
    float release_ratio;      // released below baseline * (1 + release_ratio)
    uint8_t debounce;         // consecutive samples needed to change state
    uint8_t calib_samples;    // samples averaged into the first baseline
    uint32_t tap_max_ms;      // longest contact still reported as a tap
    uint32_t hold_ms;         // contact time that becomes a hold
    float drift_alpha;        // baseline tracking weight per untouched sample
} nb_touch_config_t;

typedef struct {
    nb_touch_config_t cfg;
    float baseline;
    uint32_t calib_sum;
    uint8_t calib_count;
    uint8_t streak;           // consecutive samples disagreeing with state
    bool touched;
    bool hold_sent;
    uint32_t press_ms;
} nb_touch_t;

// Defaults sized for a copper-tape pad sampled every 20 ms.
nb_touch_config_t nb_touch_default_config(void);

void nb_touch_init(nb_touch_t *touch, const nb_touch_config_t *cfg);

bool nb_touch_is_calibrated(const nb_touch_t *touch);

// Feeds one raw reading and returns at most one event for it.
nb_touch_event_t nb_touch_update(nb_touch_t *touch, uint32_t raw, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // NB_TOUCH_H
