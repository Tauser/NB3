#include <math.h>

#include "nb_personality.h"
#include "nb_test.h"

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-5f)

static void test_default_is_todays_noise(void) {
    nb_personality_t p = nb_personality_default();
    NB_CHECK(NEAR(p.temperament.valence, 0.10f));
    NB_CHECK(NEAR(p.temperament.arousal, 0.05f));
    NB_CHECK(NEAR(p.reactivity, 1.0f));
    NB_CHECK_EQ_INT(120000, p.recovery_ms);
    NB_CHECK(NEAR(p.negative_slowdown, 2.0f));
    NB_CHECK(NEAR(p.initiative, 0.5f));
    NB_CHECK(!nb_personality_clamp(&p));  // the default is in range
}

static void test_a_full_file_overrides_everything(void) {
    nb_personality_t p = nb_personality_default();
    const char *text =
        "# A calmer, more cheerful Noise\n"
        "temperament_valence = 0.2\n"
        "temperament_arousal=-0.1\n"
        "  reactivity = 0.8   # a bit less reactive\n"
        "recovery_s = 90\n"
        "negative_slowdown = 1.5\n"
        "initiative = 0.3\n";
    nb_personality_parse_result_t r = nb_personality_parse(text, &p);
    NB_CHECK_EQ_INT(6, r.applied);
    NB_CHECK_EQ_INT(0, r.clamped + r.rejected + r.unknown);
    NB_CHECK(NEAR(p.temperament.valence, 0.2f));
    NB_CHECK(NEAR(p.temperament.arousal, -0.1f));
    NB_CHECK(NEAR(p.reactivity, 0.8f));
    NB_CHECK_EQ_INT(90000, p.recovery_ms);
    NB_CHECK(NEAR(p.negative_slowdown, 1.5f));
    NB_CHECK(NEAR(p.initiative, 0.3f));
}

static void test_a_partial_file_keeps_the_rest(void) {
    nb_personality_t p = nb_personality_default();
    nb_personality_parse_result_t r = nb_personality_parse("reactivity = 1.5\r\n", &p);
    NB_CHECK_EQ_INT(1, r.applied);
    NB_CHECK(NEAR(p.reactivity, 1.5f));
    NB_CHECK(NEAR(p.temperament.valence, 0.10f));
    NB_CHECK_EQ_INT(120000, p.recovery_ms);
}

static void test_out_of_range_values_are_clamped(void) {
    nb_personality_t p = nb_personality_default();
    const char *text =
        "temperament_valence = 3\n"
        "reactivity = 0\n"
        "recovery_s = 5\n"
        "negative_slowdown = 100\n"
        "initiative = -1\n"
        "temperament_arousal = 0.0\n";
    nb_personality_parse_result_t r = nb_personality_parse(text, &p);
    NB_CHECK_EQ_INT(6, r.applied);
    NB_CHECK_EQ_INT(5, r.clamped);  // the last line is in range
    NB_CHECK(NEAR(p.temperament.valence, 0.5f));
    NB_CHECK(NEAR(p.reactivity, 0.25f));
    NB_CHECK_EQ_INT(20000, p.recovery_ms);
    NB_CHECK(NEAR(p.negative_slowdown, 4.0f));
    NB_CHECK(NEAR(p.initiative, 0.0f));
    NB_CHECK(NEAR(p.temperament.arousal, 0.0f));
}

static void test_bad_lines_are_skipped(void) {
    nb_personality_t p = nb_personality_default();
    const char *text =
        "reactivity = lots\n"         // not a number
        "reactivity = 1.2x\n"         // trailing junk
        "recovery_s =\n"              // empty
        "nan_please = 1\n"            // unknown key
        "just some words\n"           // not a setting
        "\n"
        "= 0.3\n"                     // no key
        "initiative = nan\n";         // NaN
    nb_personality_parse_result_t r = nb_personality_parse(text, &p);
    NB_CHECK_EQ_INT(0, r.applied);
    NB_CHECK_EQ_INT(4, r.rejected);
    NB_CHECK_EQ_INT(2, r.unknown);
    nb_personality_t d = nb_personality_default();
    NB_CHECK(NEAR(p.reactivity, d.reactivity));
    NB_CHECK(NEAR(p.initiative, d.initiative));
    NB_CHECK_EQ_INT(d.recovery_ms, p.recovery_ms);
}

static void test_huge_recovery_is_clamped_not_wrapped(void) {
    nb_personality_t p = nb_personality_default();
    nb_personality_parse("recovery_s = 1e12\n", &p);
    NB_CHECK_EQ_INT(600000, p.recovery_ms);
    nb_personality_parse("recovery_s = -30\n", &p);
    NB_CHECK_EQ_INT(20000, p.recovery_ms);
}

static void test_overlong_line_does_not_overflow(void) {
    nb_personality_t p = nb_personality_default();
    char text[400];
    memset(text, ' ', sizeof(text));
    memcpy(text, "reactivity = 0.9", 16);
    text[sizeof(text) - 2] = '\n';
    text[sizeof(text) - 1] = '\0';
    nb_personality_parse_result_t r = nb_personality_parse(text, &p);
    NB_CHECK_EQ_INT(1, r.applied);
    NB_CHECK(NEAR(p.reactivity, 0.9f));
}

static void test_affect_view_carries_the_mood_fields(void) {
    nb_personality_t p = nb_personality_default();
    p.reactivity = 0.6f;
    p.recovery_ms = 90000u;
    nb_affect_personality_t a = nb_personality_affect(&p);
    NB_CHECK(NEAR(a.reactivity, 0.6f));
    NB_CHECK_EQ_INT(90000, a.recovery_ms);
    NB_CHECK(NEAR(a.temperament.valence, 0.10f));
}

static void test_null_is_safe(void) {
    nb_personality_t p = nb_personality_default();
    nb_personality_parse_result_t r = nb_personality_parse(NULL, &p);
    NB_CHECK_EQ_INT(0, r.applied);
    r = nb_personality_parse("reactivity = 1", NULL);
    NB_CHECK_EQ_INT(0, r.applied);
    NB_CHECK(!nb_personality_clamp(NULL));
    nb_affect_personality_t a = nb_personality_affect(NULL);
    NB_CHECK(NEAR(a.reactivity, 1.0f));
}

static void test_files_from_any_editor_read_the_same(void) {
    static const char kIni[] = "# calmo\r\nreactivity = 0.6\r\nrecovery_s = 300\r\n";
    uint8_t utf16le[2 + 2 * sizeof(kIni)], utf16be[2 + 2 * sizeof(kIni)], bom8[3 + sizeof(kIni)];
    utf16le[0] = 0xFF;
    utf16le[1] = 0xFE;
    utf16be[0] = 0xFE;
    utf16be[1] = 0xFF;
    for (size_t i = 0; i < sizeof(kIni) - 1; i++) {
        utf16le[2 + 2 * i] = (uint8_t)kIni[i];
        utf16le[3 + 2 * i] = 0;
        utf16be[2 + 2 * i] = 0;
        utf16be[3 + 2 * i] = (uint8_t)kIni[i];
    }
    const size_t wide_len = 2 + 2 * (sizeof(kIni) - 1);
    bom8[0] = 0xEF;
    bom8[1] = 0xBB;
    bom8[2] = 0xBF;
    memcpy(bom8 + 3, kIni, sizeof(kIni) - 1);
    const struct {
        const uint8_t *bytes;
        size_t len;
    } files[] = {
        {(const uint8_t *)kIni, sizeof(kIni) - 1},
        {bom8, 3 + sizeof(kIni) - 1},
        {utf16le, wide_len},
        {utf16be, wide_len},
        {utf16le + 2, wide_len - 2},  // no BOM: told by the zero bytes
    };
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        char text[256];
        nb_personality_text(files[f].bytes, files[f].len, text, sizeof(text));
        nb_personality_t p = nb_personality_default();
        const nb_personality_parse_result_t r = nb_personality_parse(text, &p);
        NB_CHECK_EQ_INT(2, r.applied);
        NB_CHECK_EQ_INT(0, r.unknown);
        NB_CHECK(p.reactivity > 0.59f && p.reactivity < 0.61f);
    }
    // Accents become '?', never a broken byte; tiny buffers stay terminated.
    char text[8];
    const uint8_t accented[] = {'c', 0xC3, 0xA1, 'f', 0xC3, 0xA9};
    NB_CHECK_EQ_INT(4, nb_personality_text(accented, sizeof(accented), text, sizeof(text)));
    NB_CHECK(strcmp(text, "c?f?") == 0);
    NB_CHECK_EQ_INT(2, nb_personality_text((const uint8_t *)"abcdef", 6, text, 3));
    NB_CHECK(strcmp(text, "ab") == 0);
    NB_CHECK_EQ_INT(0, nb_personality_text(NULL, 4, text, sizeof(text)));
}

int main(void) {
    NB_RUN(test_default_is_todays_noise);
    NB_RUN(test_a_full_file_overrides_everything);
    NB_RUN(test_a_partial_file_keeps_the_rest);
    NB_RUN(test_out_of_range_values_are_clamped);
    NB_RUN(test_bad_lines_are_skipped);
    NB_RUN(test_huge_recovery_is_clamped_not_wrapped);
    NB_RUN(test_overlong_line_does_not_overflow);
    NB_RUN(test_affect_view_carries_the_mood_fields);
    NB_RUN(test_null_is_safe);
    NB_RUN(test_files_from_any_editor_read_the_same);
    NB_TEST_MAIN_END();
}
