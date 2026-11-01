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
