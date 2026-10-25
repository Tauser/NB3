#ifndef NB_EXPRESSION_H
#define NB_EXPRESSION_H

#ifdef __cplusplus
extern "C" {
#endif

// Facial expressions the NB3 face can show. The cloud sends a wider emotion
// vocabulary; nb_expression_from_emotion() folds it into this set so the
// face only has to draw a handful of distinct poses well.
typedef enum {
    NB_EXPR_NEUTRAL = 0,
    NB_EXPR_HAPPY,
    NB_EXPR_SAD,
    NB_EXPR_ANGRY,
    NB_EXPR_SURPRISED,
    NB_EXPR_THINKING,
    NB_EXPR_SLEEPY,
    NB_EXPR_LOVING,
    NB_EXPR_EMBARRASSED,
    NB_EXPR_CONFUSED,
    NB_EXPR_COUNT
} nb_expression_t;

// Geometry targets for one expression. Ranges are normalized so the renderer
// owns pixel sizes:
//   eye_open    0 closed .. 1 fully open
//   eye_smile   0 flat .. 1 lower lid pushed up (smiling eyes)
//   brow_tilt  -1 inner ends down (angry) .. 1 inner ends up (sad/worried)
//   mouth_curve -1 frown .. 1 smile
//   mouth_open  0 closed .. 1 wide open
//   blush       0 none .. 1 full
typedef struct {
    float eye_open;
    float eye_smile;
    float brow_tilt;
    float mouth_curve;
    float mouth_open;
    float blush;
} nb_face_params_t;

// Maps an emotion name from the xiaozhi protocol ("happy", "thinking", ...)
// to an expression. NULL or unknown names map to NB_EXPR_NEUTRAL.
nb_expression_t nb_expression_from_emotion(const char *emotion);

// Stable lowercase name, e.g. for logs. Out-of-range values return "neutral".
const char *nb_expression_name(nb_expression_t expression);

// Geometry targets for an expression. Out-of-range values return neutral.
nb_face_params_t nb_expression_params(nb_expression_t expression);

#ifdef __cplusplus
}
#endif

#endif // NB_EXPRESSION_H
