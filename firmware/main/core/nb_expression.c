#include "nb_expression.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *emotion;
    nb_expression_t expression;
} emotion_entry_t;

// The xiaozhi emotion vocabulary, folded into the NB3 expression set.
static const emotion_entry_t kEmotionTable[] = {
    {"neutral", NB_EXPR_NEUTRAL},
    {"relaxed", NB_EXPR_NEUTRAL},
    {"cool", NB_EXPR_NEUTRAL},
    {"confident", NB_EXPR_HAPPY},
    {"happy", NB_EXPR_HAPPY},
    {"laughing", NB_EXPR_HAPPY},
    {"funny", NB_EXPR_HAPPY},
    {"winking", NB_EXPR_HAPPY},
    {"delicious", NB_EXPR_HAPPY},
    {"silly", NB_EXPR_HAPPY},
    {"sad", NB_EXPR_SAD},
    {"crying", NB_EXPR_SAD},
    {"angry", NB_EXPR_ANGRY},
    {"surprised", NB_EXPR_SURPRISED},
    {"shocked", NB_EXPR_SURPRISED},
    {"thinking", NB_EXPR_THINKING},
    {"sleepy", NB_EXPR_SLEEPY},
    {"loving", NB_EXPR_LOVING},
    {"kissy", NB_EXPR_LOVING},
    {"embarrassed", NB_EXPR_EMBARRASSED},
    {"confused", NB_EXPR_CONFUSED},
};

static const char *const kNames[NB_EXPR_COUNT] = {
    [NB_EXPR_NEUTRAL] = "neutral",         [NB_EXPR_HAPPY] = "happy",
    [NB_EXPR_SAD] = "sad",                 [NB_EXPR_ANGRY] = "angry",
    [NB_EXPR_SURPRISED] = "surprised",     [NB_EXPR_THINKING] = "thinking",
    [NB_EXPR_SLEEPY] = "sleepy",           [NB_EXPR_LOVING] = "loving",
    [NB_EXPR_EMBARRASSED] = "embarrassed", [NB_EXPR_CONFUSED] = "confused",
};

//                                      open  smile  brow  mouth  mopen  blush
static const nb_face_params_t kParams[NB_EXPR_COUNT] = {
    [NB_EXPR_NEUTRAL] = {1.00f, 0.00f, 0.00f, 0.15f, 0.00f, 0.00f},
    [NB_EXPR_HAPPY] = {0.85f, 0.70f, 0.20f, 0.90f, 0.30f, 0.20f},
    [NB_EXPR_SAD] = {0.70f, 0.00f, 0.80f, -0.70f, 0.00f, 0.00f},
    [NB_EXPR_ANGRY] = {0.75f, 0.00f, -0.90f, -0.50f, 0.10f, 0.00f},
    [NB_EXPR_SURPRISED] = {1.00f, 0.00f, 0.60f, 0.00f, 0.80f, 0.00f},
    [NB_EXPR_THINKING] = {0.80f, 0.00f, 0.30f, -0.10f, 0.00f, 0.00f},
    [NB_EXPR_SLEEPY] = {0.25f, 0.00f, 0.10f, 0.00f, 0.10f, 0.00f},
    [NB_EXPR_LOVING] = {0.80f, 0.80f, 0.30f, 0.70f, 0.10f, 0.80f},
    [NB_EXPR_EMBARRASSED] = {0.70f, 0.40f, 0.50f, 0.20f, 0.00f, 1.00f},
    [NB_EXPR_CONFUSED] = {0.90f, 0.00f, 0.40f, -0.20f, 0.10f, 0.00f},
};

nb_expression_t nb_expression_from_emotion(const char *emotion) {
    if (emotion == NULL) {
        return NB_EXPR_NEUTRAL;
    }
    for (size_t i = 0; i < sizeof(kEmotionTable) / sizeof(kEmotionTable[0]); i++) {
        if (strcmp(emotion, kEmotionTable[i].emotion) == 0) {
            return kEmotionTable[i].expression;
        }
    }
    return NB_EXPR_NEUTRAL;
}

static int is_valid(nb_expression_t expression) {
    return (int)expression >= 0 && expression < NB_EXPR_COUNT;
}

const char *nb_expression_name(nb_expression_t expression) {
    return is_valid(expression) ? kNames[expression] : kNames[NB_EXPR_NEUTRAL];
}

nb_face_params_t nb_expression_params(nb_expression_t expression) {
    return is_valid(expression) ? kParams[expression] : kParams[NB_EXPR_NEUTRAL];
}
