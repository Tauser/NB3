#include "nb_overlay_anchor.h"

// The face scale breathes and leans with the head pitch; the overlay follows
// it only partly, so a pulse of the face does not make the adornment throb.
#define FOLLOW_SCALE 0.5f

void nb_overlay_anchor_from_pose(const nb_avatar_pose_t *pose, float design_scale,
                                 nb_overlay_anchor_t *out) {
    const float mid_x = 0.5f * (pose->left_eye.x + pose->right_eye.x);
    const float mid_y = 0.5f * (pose->left_eye.y + pose->right_eye.y);
    const float face = pose->face_scale;
    out->dx = (pose->face_x + mid_x) * design_scale * face;
    out->dy = (pose->face_y + mid_y) * design_scale * face;
    out->scale = NB_OVERLAY_SIZE * (1.0f + (face - 1.0f) * FOLLOW_SCALE);
}

void nb_overlay_clamp_anchor(const nb_overlay_rect_t *extent, float screen_w, float screen_h,
                             nb_overlay_anchor_t *anchor) {
    const float cx = screen_w * 0.5f;
    const float cy = screen_h * 0.5f;
    const float s = anchor->scale;
    // Pull back from the right and bottom edges first, then push in from the
    // left and top, so an extent too big for the screen keeps its top left.
    float shift_x = 0.0f;
    float shift_y = 0.0f;
    const float right = cx + anchor->dx + extent->x1 * s;
    const float bottom = cy + anchor->dy + extent->y1 * s;
    if (right > screen_w) {
        shift_x = screen_w - right;
    }
    if (bottom > screen_h) {
        shift_y = screen_h - bottom;
    }
    const float left = cx + anchor->dx + shift_x + extent->x0 * s;
    const float top = cy + anchor->dy + shift_y + extent->y0 * s;
    if (left < 0.0f) {
        shift_x -= left;
    }
    if (top < 0.0f) {
        shift_y -= top;
    }
    anchor->dx += shift_x;
    anchor->dy += shift_y;
}

bool nb_overlay_hidden_by_banner(const nb_overlay_rect_t *extent, const nb_overlay_anchor_t *anchor,
                                 float screen_h, float banner_top) {
    const float bottom = screen_h * 0.5f + anchor->dy + extent->y1 * anchor->scale;
    return bottom > banner_top;
}
