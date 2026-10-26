#include <math.h>
#include <string.h>

#include "nb_json.h"
#include "nb_test.h"

static nb_json_tok_t toks[64];

static int parse(const char *s) { return nb_json_parse(s, strlen(s), toks, 64); }

static void test_writer_round_trip(void) {
    char buf[256];
    nb_json_writer_t w;
    nb_jw_init(&w, buf, sizeof(buf));
    nb_jw_object_begin(&w);
    nb_jw_key(&w, "text");
    nb_jw_string(&w, "diz \"oi\" \\ café\t!");
    nb_jw_key(&w, "n");
    nb_jw_int(&w, -1234567890123LL);
    nb_jw_key(&w, "f");
    nb_jw_float(&w, 0.25f);
    nb_jw_key(&w, "list");
    nb_jw_array_begin(&w);
    nb_jw_bool(&w, true);
    nb_jw_int(&w, 7);
    nb_jw_array_end(&w);
    nb_jw_object_end(&w);
    const size_t n = nb_jw_finish(&w);
    NB_CHECK(n > 0);
    NB_CHECK_EQ_STR("{\"text\":\"diz \\\"oi\\\" \\\\ café\\u0009!\",\"n\":-1234567890123,"
                    "\"f\":0.250,\"list\":[true,7]}",
                    buf);

    NB_CHECK(parse(buf) > 0);
    char text[64];
    int i = nb_json_find(buf, toks, 0, "text");
    // The tab was escaped by the writer; control characters are not text.
    NB_CHECK(i > 0 && !nb_json_string(buf, &toks[i], text, sizeof(text)));
    int64_t v;
    i = nb_json_find(buf, toks, 0, "n");
    NB_CHECK(nb_json_int(buf, &toks[i], INT64_MIN, INT64_MAX, &v));
    NB_CHECK_EQ_INT(-1234567890123LL, v);
    double f;
    i = nb_json_find(buf, toks, 0, "f");
    NB_CHECK(nb_json_number(buf, &toks[i], &f) && fabs(f - 0.25) < 1e-9);
    i = nb_json_find(buf, toks, 0, "list");
    NB_CHECK_EQ_INT(NB_JSON_ARRAY, toks[i].type);
    NB_CHECK_EQ_INT(2, toks[i].size);
    NB_CHECK_EQ_INT(-1, nb_json_find(buf, toks, 0, "missing"));
}

static void test_strings_unescape(void) {
    const char *s = "{\"a\":\"caf\\u00e9 \\\"x\\\" \\/ \\ud83d\\ude00\"}";
    NB_CHECK_EQ_INT(3, parse(s));
    char out[32];
    NB_CHECK(nb_json_string(s, &toks[2], out, sizeof(out)));
    NB_CHECK_EQ_STR("café \"x\" / \xF0\x9F\x98\x80", out);
    char small[4];
    NB_CHECK(!nb_json_string(s, &toks[2], small, sizeof(small)));  // does not fit

    const char *bad[] = {
        "\"\\u0000\"",        // NUL
        "\"\\ud83d\"",        // lone high surrogate
        "\"\\ude00\"",        // lone low surrogate
    };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        NB_CHECK_EQ_INT(1, parse(bad[k]));
        NB_CHECK(!nb_json_string(bad[k], &toks[0], out, sizeof(out)));
    }
}

static void test_integers_are_strict(void) {
    int64_t v;
    const char *s = "[1.5,1e3,99999999999999999999,-0,42]";
    NB_CHECK_EQ_INT(6, parse(s));
    NB_CHECK(!nb_json_int(s, &toks[1], INT64_MIN, INT64_MAX, &v));
    NB_CHECK(!nb_json_int(s, &toks[2], INT64_MIN, INT64_MAX, &v));
    NB_CHECK(!nb_json_int(s, &toks[3], INT64_MIN, INT64_MAX, &v));  // overflow
    NB_CHECK(nb_json_int(s, &toks[4], INT64_MIN, INT64_MAX, &v) && v == 0);
    NB_CHECK(!nb_json_int(s, &toks[5], 0, 10, &v));  // out of range
}

static void test_malformed_text_is_rejected(void) {
    const char *bad[] = {
        "", "{", "}", "[1,]", "{\"a\"}", "{\"a\":}", "{a:1}", "[01]", "[1.]", "[-]",
        "[1e]", "\"x", "\"\\x\"", "\"\\u12\"", "tru", "nul", "[1] 2", "{\"a\":1,}",
        "\"a\nb\"", "[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]",
    };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        if (parse(bad[k]) >= 0) {
            fprintf(stderr, "accepted: %s\n", bad[k]);
            NB_CHECK(false);
        }
    }
    NB_CHECK_EQ_INT(-1, nb_json_parse("[1,2,3]", 7, toks, 3));  // too many tokens
}

static void test_every_truncation_of_an_object_is_rejected(void) {
    const char *s = "{\"id\":12,\"text\":\"gosta de café\",\"tags\":[1,2],\"ok\":true}";
    const size_t n = strlen(s);
    NB_CHECK(parse(s) > 0);
    for (size_t cut = 0; cut < n; cut++) {
        NB_CHECK(nb_json_parse(s, cut, toks, 64) < 0);
    }
}

static void test_writer_overflow_fails_cleanly(void) {
    char buf[8];
    nb_json_writer_t w;
    nb_jw_init(&w, buf, sizeof(buf));
    nb_jw_object_begin(&w);
    nb_jw_key(&w, "longer key");
    nb_jw_int(&w, 1);
    nb_jw_object_end(&w);
    NB_CHECK_EQ_INT(0, nb_jw_finish(&w));

    char big[32];
    nb_jw_init(&w, big, sizeof(big));
    nb_jw_object_begin(&w);
    nb_jw_key(&w, "a");
    nb_jw_object_end(&w);  // a key without a value
    NB_CHECK_EQ_INT(0, nb_jw_finish(&w));
}

int main(void) {
    NB_RUN(test_writer_round_trip);
    NB_RUN(test_strings_unescape);
    NB_RUN(test_integers_are_strict);
    NB_RUN(test_malformed_text_is_rejected);
    NB_RUN(test_every_truncation_of_an_object_is_rejected);
    NB_RUN(test_writer_overflow_fails_cleanly);
    NB_TEST_MAIN_END();
}
