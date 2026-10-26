#ifndef NB_FACE_LINK_H
#define NB_FACE_LINK_H

#include <stdbool.h>

#include "nb_avatar.h"
#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// Drives the avatar from the arbiter's winning expression. The arbiter is
// the source of truth for how long an expression lasts: a reaction holds on
// screen while its proposal wins instead of drifting back to IDLE when its
// animation ends, and going back to IDLE lets a gesture still in its first
// pass finish.

// The avatar animation that draws an expression. Expressions without an
// animation of their own borrow the closest one (LOVING draws as HAPPY,
// EMBARRASSED as CURIOUS).
nb_avatar_expression_t nb_face_link_avatar_for(nb_expression_t expression);

// Call when the winning expression changes.
bool nb_face_link_show(nb_avatar_core_state_t *avatar, nb_expression_t expression);

#ifdef __cplusplus
}
#endif

#endif // NB_FACE_LINK_H
