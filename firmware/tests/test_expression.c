#include "nb_expression.h"
#include "nb_test.h"

static void test_known_emotions_map_to_their_expression(void) {
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_expression_from_emotion("happy"));
    NB_CHECK_EQ_INT(NB_EXPR_HAPPY, nb_expression_from_emotion("laughing"));
    NB_CHECK_EQ_INT(NB_EXPR_SAD, nb_expression_from_emotion("crying"));
    NB_CHECK_EQ_INT(NB_EXPR_ANGRY, nb_expression_from_emotion("angry"));
    NB_CHECK_EQ_INT(NB_EXPR_SURPRISED, nb_expression_from_emotion("shocked"));
    NB_CHECK_EQ_INT(NB_EXPR_THINKING, nb_expression_from_emotion("thinking"));
    NB_CHECK_EQ_INT(NB_EXPR_SLEEPY, nb_expression_from_emotion("sleepy"));
    NB_CHECK_EQ_INT(NB_EXPR_LOVING, nb_expression_from_emotion("kissy"));
    NB_CHECK_EQ_INT(NB_EXPR_EMBARRASSED, nb_expression_from_emotion("embarrassed"));
    NB_CHECK_EQ_INT(NB_EXPR_CONFUSED, nb_expression_from_emotion("confused"));
}

static void test_every_protocol_emotion_is_recognized(void) {
    // Full xiaozhi vocabulary: none of these may fall through to the
    // unknown-name path, which also yields neutral. "neutral", "relaxed" and
    // "cool" are intentionally neutral, so they are checked by name instead.
    static const char *const non_neutral[] = {
        "happy", "laughing", "funny", "sad", "angry", "crying", "loving",
        "embarrassed", "surprised", "shocked", "thinking", "winking",
        "delicious", "kissy", "confident", "sleepy", "silly", "confused",
    };
    for (size_t i = 0; i < sizeof(non_neutral) / sizeof(non_neutral[0]); i++) {
        NB_CHECK(nb_expression_from_emotion(non_neutral[i]) != NB_EXPR_NEUTRAL);
    }
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_expression_from_emotion("relaxed"));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_expression_from_emotion("cool"));
}

static void test_unknown_or_null_emotion_is_neutral(void) {
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_expression_from_emotion(NULL));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_expression_from_emotion(""));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_expression_from_emotion("HAPPY"));
    NB_CHECK_EQ_INT(NB_EXPR_NEUTRAL, nb_expression_from_emotion("ecstatic"));
}

static void test_names_round_trip(void) {
    for (int e = 0; e < NB_EXPR_COUNT; e++) {
        NB_CHECK_EQ_INT(e, nb_expression_from_emotion(nb_expression_name((nb_expression_t)e)));
    }
    NB_CHECK_EQ_STR("neutral", nb_expression_name(NB_EXPR_COUNT));
}

static void test_params_stay_in_range(void) {
    for (int e = 0; e < NB_EXPR_COUNT; e++) {
        nb_face_params_t p = nb_expression_params((nb_expression_t)e);
        NB_CHECK(p.eye_open >= 0.0f && p.eye_open <= 1.0f);
        NB_CHECK(p.eye_smile >= 0.0f && p.eye_smile <= 1.0f);
        NB_CHECK(p.brow_tilt >= -1.0f && p.brow_tilt <= 1.0f);
        NB_CHECK(p.mouth_curve >= -1.0f && p.mouth_curve <= 1.0f);
        NB_CHECK(p.mouth_open >= 0.0f && p.mouth_open <= 1.0f);
        NB_CHECK(p.blush >= 0.0f && p.blush <= 1.0f);
    }
}

static void test_expressions_read_differently(void) {
    nb_face_params_t happy = nb_expression_params(NB_EXPR_HAPPY);
    nb_face_params_t sad = nb_expression_params(NB_EXPR_SAD);
    nb_face_params_t angry = nb_expression_params(NB_EXPR_ANGRY);
    nb_face_params_t sleepy = nb_expression_params(NB_EXPR_SLEEPY);
    NB_CHECK(happy.mouth_curve > 0.0f && sad.mouth_curve < 0.0f);
    NB_CHECK(angry.brow_tilt < 0.0f && sad.brow_tilt > 0.0f);
    NB_CHECK(sleepy.eye_open < nb_expression_params(NB_EXPR_NEUTRAL).eye_open);
}

static void test_invalid_expression_params_are_neutral(void) {
    nb_face_params_t p = nb_expression_params(NB_EXPR_COUNT);
    nb_face_params_t n = nb_expression_params(NB_EXPR_NEUTRAL);
    NB_CHECK(p.eye_open == n.eye_open && p.mouth_curve == n.mouth_curve);
}

int main(void) {
    NB_RUN(test_known_emotions_map_to_their_expression);
    NB_RUN(test_every_protocol_emotion_is_recognized);
    NB_RUN(test_unknown_or_null_emotion_is_neutral);
    NB_RUN(test_names_round_trip);
    NB_RUN(test_params_stay_in_range);
    NB_RUN(test_expressions_read_differently);
    NB_RUN(test_invalid_expression_params_are_neutral);
    NB_TEST_MAIN_END();
}
