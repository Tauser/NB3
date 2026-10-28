#ifndef NB_HEAD_GESTURE_H
#define NB_HEAD_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Head moves as waypoints for the motion task: look somewhere, nod, shake,
// back to center. Angles are the robot's own (yaw > 0 = it looks to its
// left, pitch > 0 = it looks up); the geometry turns them into servo counts.
// Every waypoint is clamped into the limits, so near an edge a gesture just
// gets smaller, and a nod or a shake ends where it began. motion_safety still
// vets each waypoint: this core only keeps the request sensible.

#define NB_HEAD_WAYPOINTS_MAX 12
#define NB_HEAD_REPEATS_MAX 4

typedef enum {
    NB_HEAD_LOOK = 0,
    NB_HEAD_CENTER,
    NB_HEAD_NOD,    // yes: down and up
    NB_HEAD_SHAKE,  // no: side to side
} nb_head_move_t;

typedef struct {
    uint16_t center_pan, center_tilt;  // counts looking straight ahead
    uint16_t pan_min, pan_max;         // counts, inclusive
    uint16_t tilt_min, tilt_max;
    int8_t pan_sign, tilt_sign;        // +1 or -1: how the servos are mounted
    float counts_per_deg;
} nb_head_geometry_t;

typedef struct {
    uint16_t pan, tilt;
    uint16_t time_ms;  // to get there from the previous one
} nb_head_waypoint_t;

typedef struct {
    nb_head_waypoint_t points[NB_HEAD_WAYPOINTS_MAX];
    int count;
    bool clamped;       // some point was pulled into the limits
    uint32_t total_ms;
} nb_head_plan_t;

// From the current position (counts). yaw and pitch are used by LOOK only;
// repeats by NOD and SHAKE (1..NB_HEAD_REPEATS_MAX). False for a bad move
// or geometry.
bool nb_head_plan(const nb_head_geometry_t *g, nb_head_move_t move, float yaw_deg,
                  float pitch_deg, int repeats, uint16_t pan_now, uint16_t tilt_now,
                  nb_head_plan_t *out);

// The angles of a position, for reporting.
float nb_head_yaw_deg(const nb_head_geometry_t *g, uint16_t pan);
float nb_head_pitch_deg(const nb_head_geometry_t *g, uint16_t tilt);

const char *nb_head_move_name(nb_head_move_t move);
bool nb_head_move_from_name(const char *name, nb_head_move_t *move);

#ifdef __cplusplus
}
#endif

#endif  // NB_HEAD_GESTURE_H
