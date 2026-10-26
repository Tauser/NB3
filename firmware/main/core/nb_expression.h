#ifndef NB_EXPRESSION_H
#define NB_EXPRESSION_H

#ifdef __cplusplus
extern "C" {
#endif

// What the robot means to express, independent of how the face draws it.
// The cloud sends a wider emotion vocabulary; nb_expression_from_emotion()
// folds it into this set. The face maps each value to an avatar animation.
typedef enum {
    NB_EXPR_NEUTRAL = 0,
    NB_EXPR_LISTENING,
    NB_EXPR_THINKING,
    NB_EXPR_HAPPY,
    NB_EXPR_EXCITED,
    NB_EXPR_CURIOUS,
    NB_EXPR_CONFUSED,
    NB_EXPR_ANGRY,
    NB_EXPR_SURPRISED,
    NB_EXPR_SAD,
    NB_EXPR_SLEEPY,
    NB_EXPR_DIZZY,
    NB_EXPR_LOVING,
    NB_EXPR_EMBARRASSED,
    NB_EXPR_CONTENT,  // the mood at rest after warmth: calm, not the face of being petted
    NB_EXPR_COUNT
} nb_expression_t;

// Maps an emotion name from the xiaozhi protocol ("happy", "thinking", ...)
// or an expression name to an expression. NULL or unknown names map to
// NB_EXPR_NEUTRAL.
nb_expression_t nb_expression_from_emotion(const char *emotion);

// Stable lowercase name, e.g. for logs and MCP. Out-of-range is "neutral".
const char *nb_expression_name(nb_expression_t expression);

#ifdef __cplusplus
}
#endif

#endif // NB_EXPRESSION_H
