#include "nb_face_link.h"

nb_avatar_expression_t nb_face_link_avatar_for(nb_expression_t expression)
{
    switch (expression) {
    case NB_EXPR_LISTENING: return NB_AVATAR_EXPR_LISTENING;
    case NB_EXPR_THINKING: return NB_AVATAR_EXPR_THINKING;
    case NB_EXPR_HAPPY: return NB_AVATAR_EXPR_HAPPY;
    case NB_EXPR_EXCITED: return NB_AVATAR_EXPR_EXCITED;
    case NB_EXPR_CURIOUS: return NB_AVATAR_EXPR_CURIOUS;
    case NB_EXPR_CONFUSED: return NB_AVATAR_EXPR_CONFUSED;
    case NB_EXPR_ANGRY: return NB_AVATAR_EXPR_ANGRY;
    case NB_EXPR_SURPRISED: return NB_AVATAR_EXPR_SURPRISED;
    case NB_EXPR_SAD: return NB_AVATAR_EXPR_SAD;
    case NB_EXPR_SLEEPY: return NB_AVATAR_EXPR_SLEEPY;
    case NB_EXPR_DIZZY: return NB_AVATAR_EXPR_DIZZY;
    case NB_EXPR_LOVING: return NB_AVATAR_EXPR_LOVING;
    case NB_EXPR_EMBARRASSED: return NB_AVATAR_EXPR_EMBARRASSED;
    case NB_EXPR_CONTENT: return NB_AVATAR_EXPR_CONTENT;
    default: return NB_AVATAR_EXPR_IDLE;
    }
}

bool nb_face_link_show(nb_avatar_core_state_t *avatar, nb_expression_t expression)
{
    const nb_avatar_expression_t target = nb_face_link_avatar_for(expression);
    if (target == NB_AVATAR_EXPR_IDLE) {
        return nb_avatar_core_release(avatar, NB_AVATAR_EXPR_IDLE);
    }
    return nb_avatar_core_hold(avatar, target);
}
