#ifndef NB_OVERLAY_ANCHOR_H
#define NB_OVERLAY_ANCHOR_H

#include <stdbool.h>

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

// What a channel draws, in the layout units of the overlay (screen pixels from
// the eyes' rest centre, before the anchor's scale).
typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
} nb_overlay_rect_t;

// Moves the anchor so the whole extent stays on a screen_w x screen_h screen
// whose centre is where the overlay origin sits at rest, instead of being cut
// off at an edge. An extent larger than the screen keeps its top left corner.
void nb_overlay_clamp_anchor(const nb_overlay_rect_t *extent, float screen_w, float screen_h,
                             nb_overlay_anchor_t *anchor);

// The banner wins over what it covers: true when any part of the extent, as
// placed by the anchor, reaches below banner_top (a screen y; use a value at or
// beyond screen_h when there is no banner).
bool nb_overlay_hidden_by_banner(const nb_overlay_rect_t *extent, const nb_overlay_anchor_t *anchor,
                                 float screen_h, float banner_top);

#ifdef __cplusplus
}
#endif

#endif  // NB_OVERLAY_ANCHOR_H
