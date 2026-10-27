#ifndef NB_ENERGY_H
#define NB_ENERGY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Energy and boredom (docs/VIDA.md §3.3): how much the robot has in it, kept
// apart from how it feels. Energy follows the time of day and jumps up when
// someone engages the robot; boredom grows in quiet rest and drops with
// company. Neither touches the mood: they change the manner (gaze pace,
// initiative, sleepiness, sleep). Both live in RAM.

#define NB_ENERGY_DROWSY 0.35f
#define NB_ENERGY_SLEEP 0.25f
#define NB_ENERGY_SLEEP_QUIET_MS (10u * 60u * 1000u)
#define NB_ENERGY_UNKNOWN_TIME 0.8f  // target when the hour is not known

typedef struct {
    float energy;       // 0 spent .. 1 full
    float boredom;      // 0 .. 1
    uint32_t quiet_ms;  // at rest without anyone since
    bool at_rest;
} nb_energy_t;

void nb_energy_init(nb_energy_t *e);

// minute_of_day is 0..1439 local time, or -1 when the time is not synced.
// at_rest is the behavioral IDLE or SLEEPING; leaving it counts as someone
// engaging the robot.
void nb_energy_tick(nb_energy_t *e, uint32_t dt_ms, bool at_rest, int minute_of_day);

// Where energy heads at this time of day.
float nb_energy_circadian(int minute_of_day);

bool nb_energy_drowsy(const nb_energy_t *e);
bool nb_energy_wants_sleep(const nb_energy_t *e);

// The calmest arousal the gaze may show (-1..1).
float nb_energy_arousal(const nb_energy_t *e);

// Multiplier of the gap between thoughts on its own: 0 = none (drowsy),
// otherwise from 1.7 (not bored) to 0.7 (bored).
float nb_energy_initiative_gap(const nb_energy_t *e);

#ifdef __cplusplus
}
#endif

#endif  // NB_ENERGY_H
