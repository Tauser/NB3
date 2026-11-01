#ifndef NB_OVERLAY_ANCHOR_H
#define NB_OVERLAY_ANCHOR_H

#include "nb_avatar.h"

#ifdef __cplusplus
extern "C" {
#endif

// Overlays are laid out relative to the eyes at rest. The anchor tells the
// display how far the eyes have moved from there (gaze, head turn, breathing)
// and how big to draw the overlay, so an adornment keeps its place against
// the eyes instead of staying fixed on the screen.

// Overlays are drawn a little smaller than the mocks, at a rest face.
#define NB_OVERLAY_SIZE 0.8f

typedef struct {
    float dx;     // screen pixels the overlay origin moves from the face centre
    float dy;
    float scale;  // multiplies every overlay offset and size
} nb_overlay_anchor_t;

// design_scale converts the avatar's design units to screen pixels.
void nb_overlay_anchor_from_pose(const nb_avatar_pose_t *pose, float design_scale,
                                 nb_overlay_anchor_t *out);

#ifdef __cplusplus
}
#endif

#endif  // NB_OVERLAY_ANCHOR_H
