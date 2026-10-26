#include "nb_banner.h"
#include "nb_test.h"

static void test_starts_empty(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    NB_CHECK(!nb_banner_update(&b, 0));
    NB_CHECK_EQ_INT(NB_BANNER_NONE, b.kind);
}

static void test_notice_expires_after_default(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    NB_CHECK(nb_banner_show(&b, NB_BANNER_NOTICE, "Versao 1.0", 0, 1000));
    NB_CHECK_EQ_STR("Versao 1.0", b.text);
    NB_CHECK(nb_banner_update(&b, 1000 + NB_BANNER_NOTICE_DEFAULT_MS - 1));
    NB_CHECK(!nb_banner_update(&b, 1000 + NB_BANNER_NOTICE_DEFAULT_MS));
    NB_CHECK_EQ_INT(NB_BANNER_NONE, b.kind);
}

static void test_custom_timeout_and_cap(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_NOTICE, "x", 30000, 0);
    NB_CHECK(nb_banner_update(&b, 29999));
    NB_CHECK(!nb_banner_update(&b, 30000));
    nb_banner_show(&b, NB_BANNER_NOTICE, "x", 10 * 60000u, 0);
    NB_CHECK_EQ_INT(NB_BANNER_MAX_TIMEOUT_MS, b.timeout_ms);
}

static void test_error_has_longer_default(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ERROR, "Sem rede", 0, 0);
    NB_CHECK(nb_banner_update(&b, NB_BANNER_ERROR_DEFAULT_MS - 1));
    NB_CHECK(!nb_banner_update(&b, NB_BANNER_ERROR_DEFAULT_MS));
}

static void test_sticky_kinds_ignore_timeout(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ACTIVATION, "Codigo 123456", 100, 0);
    NB_CHECK(nb_banner_update(&b, 0xF0000000u));
    nb_banner_show(&b, NB_BANNER_WIFI_SETUP, "hotspot", 100, 5);
    NB_CHECK(nb_banner_update(&b, 0xF0000000u));
}

static void test_lower_priority_does_not_replace(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ACTIVATION, "123456", 0, 0);
    NB_CHECK(!nb_banner_show(&b, NB_BANNER_ERROR, "erro", 0, 10));
    NB_CHECK(!nb_banner_show(&b, NB_BANNER_NOTICE, "aviso", 0, 10));
    NB_CHECK_EQ_STR("123456", b.text);
    NB_CHECK_EQ_INT(NB_BANNER_ACTIVATION, b.kind);
}

static void test_equal_or_higher_replaces(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ERROR, "a", 0, 0);
    NB_CHECK(nb_banner_show(&b, NB_BANNER_ERROR, "b", 0, 1));
    NB_CHECK_EQ_STR("b", b.text);
    NB_CHECK(nb_banner_show(&b, NB_BANNER_WIFI_SETUP, "c", 0, 2));
    NB_CHECK_EQ_INT(NB_BANNER_WIFI_SETUP, b.kind);
}

static void test_lower_priority_allowed_after_expiry(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ERROR, "erro", 1000, 0);
    // The error is dead by now even though nobody called update.
    NB_CHECK(nb_banner_show(&b, NB_BANNER_NOTICE, "aviso", 0, 5000));
    NB_CHECK_EQ_STR("aviso", b.text);
}

static void test_clear_and_clear_kind(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ACTIVATION, "123456", 0, 0);
    nb_banner_clear_kind(&b, NB_BANNER_ERROR);
    NB_CHECK(nb_banner_update(&b, 1));
    nb_banner_clear_kind(&b, NB_BANNER_ACTIVATION);
    NB_CHECK(!nb_banner_update(&b, 1));
    nb_banner_show(&b, NB_BANNER_WIFI_SETUP, "x", 0, 0);
    nb_banner_clear(&b);
    NB_CHECK(!nb_banner_update(&b, 1));
    NB_CHECK_EQ_STR("", b.text);
}

static void test_rejects_empty_and_invalid(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    NB_CHECK(!nb_banner_show(&b, NB_BANNER_NOTICE, "", 0, 0));
    NB_CHECK(!nb_banner_show(&b, NB_BANNER_NOTICE, NULL, 0, 0));
    NB_CHECK(!nb_banner_show(&b, NB_BANNER_NONE, "x", 0, 0));
    NB_CHECK(!nb_banner_show(&b, (nb_banner_kind_t)99, "x", 0, 0));
    NB_CHECK(!nb_banner_show(&b, NB_BANNER_NOTICE, "\xFF\xFE", 0, 0));
    NB_CHECK_EQ_INT(0, b.revision);
}

static void test_wraparound_expiry(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    const uint32_t start = 0xFFFFFF00u;  // 256 ms before the counter wraps
    nb_banner_show(&b, NB_BANNER_NOTICE, "x", 1000, start);
    NB_CHECK(nb_banner_update(&b, start + 999));  // past the wrap
    NB_CHECK(!nb_banner_update(&b, start + 1000));
}

static void test_truncation_keeps_utf8_whole(void) {
    // "\xC3\xA7" (c-cedilla) is 2 bytes; the leading 'a' misaligns the pairs
    // so the last one straddles the limit.
    char in[NB_BANNER_TEXT_MAX + 8];
    size_t n = 0;
    in[n++] = 'a';
    while (n < sizeof(in) - 2) {
        in[n++] = (char)0xC3;
        in[n++] = (char)0xA7;
    }
    in[n] = '\0';
    nb_banner_t b;
    nb_banner_init(&b);
    NB_CHECK(nb_banner_show(&b, NB_BANNER_NOTICE, in, 0, 0));
    size_t len = strlen(b.text);
    NB_CHECK_EQ_INT(NB_BANNER_TEXT_MAX - 1, len);  // 1 + 95 whole pairs
    NB_CHECK_EQ_INT(0xC3, (unsigned char)b.text[len - 2]);
    NB_CHECK_EQ_INT(0xA7, (unsigned char)b.text[len - 1]);
}

static void test_truncation_four_byte_and_accents(void) {
    char in[NB_BANNER_TEXT_MAX + 8];
    size_t n = 0;
    in[n++] = 'x';
    in[n++] = 'x';
    in[n++] = 'x';
    while (n < sizeof(in) - 4) {
        in[n++] = (char)0xF0;  // U+1F600
        in[n++] = (char)0x9F;
        in[n++] = (char)0x98;
        in[n++] = (char)0x80;
    }
    in[n] = '\0';
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_NOTICE, in, 0, 0);
    size_t len = strlen(b.text);
    NB_CHECK_EQ_INT(0, (len - 3) % 4);
    NB_CHECK(len + 4 > NB_BANNER_TEXT_MAX - 1);

    nb_banner_show(&b, NB_BANNER_ERROR, "Conex\xC3\xA3o n\xC3\xA3o dispon\xC3\xADvel", 0, 0);
    NB_CHECK_EQ_STR("Conex\xC3\xA3o n\xC3\xA3o dispon\xC3\xADvel", b.text);
}

static void test_malformed_tail_and_controls(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_NOTICE, "ok\xC3", 0, 0);  // cut-off char
    NB_CHECK_EQ_STR("ok", b.text);
    nb_banner_show(&b, NB_BANNER_NOTICE, "a\tb\nc\x01"
                                         "d", 0, 0);
    NB_CHECK_EQ_STR("a b\nc d", b.text);
}

static void test_revision_tracks_changes_only(void) {
    nb_banner_t b;
    nb_banner_init(&b);
    nb_banner_show(&b, NB_BANNER_ERROR, "erro", 0, 0);
    uint32_t rev = b.revision;
    nb_banner_show(&b, NB_BANNER_ERROR, "erro", 0, 100);  // refresh only
    NB_CHECK_EQ_INT(rev, b.revision);
    NB_CHECK(nb_banner_update(&b, 100 + NB_BANNER_ERROR_DEFAULT_MS - 1));  // timer restarted
    nb_banner_show(&b, NB_BANNER_ERROR, "outro", 0, 200);
    NB_CHECK(b.revision != rev);
}

int main(void) {
    NB_RUN(test_starts_empty);
    NB_RUN(test_notice_expires_after_default);
    NB_RUN(test_custom_timeout_and_cap);
    NB_RUN(test_error_has_longer_default);
    NB_RUN(test_sticky_kinds_ignore_timeout);
    NB_RUN(test_lower_priority_does_not_replace);
    NB_RUN(test_equal_or_higher_replaces);
    NB_RUN(test_lower_priority_allowed_after_expiry);
    NB_RUN(test_clear_and_clear_kind);
    NB_RUN(test_rejects_empty_and_invalid);
    NB_RUN(test_wraparound_expiry);
    NB_RUN(test_truncation_keeps_utf8_whole);
    NB_RUN(test_truncation_four_byte_and_accents);
    NB_RUN(test_malformed_tail_and_controls);
    NB_RUN(test_revision_tracks_changes_only);
    NB_TEST_MAIN_END();
}
