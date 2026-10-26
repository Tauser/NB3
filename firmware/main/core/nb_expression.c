#include "nb_expression.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *emotion;
    nb_expression_t expression;
} emotion_entry_t;

// The xiaozhi emotion vocabulary, plus the expression names themselves.
static const emotion_entry_t kEmotionTable[] = {
    {"neutral", NB_EXPR_NEUTRAL},
    {"relaxed", NB_EXPR_NEUTRAL},
    {"cool", NB_EXPR_NEUTRAL},
    {"listening", NB_EXPR_LISTENING},
    {"thinking", NB_EXPR_THINKING},
    {"happy", NB_EXPR_HAPPY},
    {"confident", NB_EXPR_HAPPY},
    {"winking", NB_EXPR_HAPPY},
    {"delicious", NB_EXPR_HAPPY},
    {"excited", NB_EXPR_EXCITED},
    {"laughing", NB_EXPR_EXCITED},
    {"funny", NB_EXPR_EXCITED},
    {"curious", NB_EXPR_CURIOUS},
    {"confused", NB_EXPR_CONFUSED},
    {"angry", NB_EXPR_ANGRY},
    {"surprised", NB_EXPR_SURPRISED},
    {"shocked", NB_EXPR_SURPRISED},
    {"sad", NB_EXPR_SAD},
    {"crying", NB_EXPR_SAD},
    {"sleepy", NB_EXPR_SLEEPY},
    {"dizzy", NB_EXPR_DIZZY},
    {"silly", NB_EXPR_DIZZY},
    {"loving", NB_EXPR_LOVING},
    {"kissy", NB_EXPR_LOVING},
    {"embarrassed", NB_EXPR_EMBARRASSED},
};

static const char *const kNames[NB_EXPR_COUNT] = {
    [NB_EXPR_NEUTRAL] = "neutral",     [NB_EXPR_LISTENING] = "listening",
    [NB_EXPR_THINKING] = "thinking",   [NB_EXPR_HAPPY] = "happy",
    [NB_EXPR_EXCITED] = "excited",     [NB_EXPR_CURIOUS] = "curious",
    [NB_EXPR_CONFUSED] = "confused",   [NB_EXPR_ANGRY] = "angry",
    [NB_EXPR_SURPRISED] = "surprised", [NB_EXPR_SAD] = "sad",
    [NB_EXPR_SLEEPY] = "sleepy",       [NB_EXPR_DIZZY] = "dizzy",
    [NB_EXPR_LOVING] = "loving",       [NB_EXPR_EMBARRASSED] = "embarrassed",
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

const char *nb_expression_name(nb_expression_t expression) {
    return (int)expression >= 0 && expression < NB_EXPR_COUNT ? kNames[expression]
                                                              : kNames[NB_EXPR_NEUTRAL];
}
