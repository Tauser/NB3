#include <string.h>

#include "nb_panel_auth.h"
#include "nb_test.h"

static void test_the_code_has_six_digits_from_the_random_number(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 4821u, 0u, 1000u);
    NB_CHECK(strcmp(a.code, "004821") == 0);
    nb_panel_auth_open(&a, 0xFFFFFFFFu, 0u, 1000u);
    NB_CHECK(strlen(a.code) == NB_PANEL_CODE_LEN);
    for (int i = 0; i < NB_PANEL_CODE_LEN; i++) {
        NB_CHECK(a.code[i] >= '0' && a.code[i] <= '9');
    }
}

static void test_the_right_code_gives_a_token_that_checks(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 123456u, 0u, 1000u);
    char token[NB_PANEL_TOKEN_LEN + 1];
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_OK,
                    nb_panel_auth_login(&a, "123456", 0x01234567u, 0x89abcdefu, 10u, token));
    NB_CHECK(strcmp(token, "76543210fedcba98") == 0);  // each word, low nibble first
    NB_CHECK(nb_panel_auth_check(&a, token, 20u));
    NB_CHECK(!nb_panel_auth_check(&a, "0000000000000000", 20u));
    NB_CHECK(!nb_panel_auth_check(&a, NULL, 20u));
    NB_CHECK(!nb_panel_auth_check(&a, "short", 20u));
}

static void test_a_wrong_code_is_refused_and_five_close_the_panel(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 111111u, 0u, 100000u);
    char token[NB_PANEL_TOKEN_LEN + 1];
    for (int i = 0; i < NB_PANEL_MAX_WRONG - 1; i++) {
        NB_CHECK_EQ_INT(NB_PANEL_LOGIN_WRONG, nb_panel_auth_login(&a, "000000", 1u, 2u, 5u, token));
    }
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_CLOSED, nb_panel_auth_login(&a, "000000", 1u, 2u, 6u, token));
    // Closed: even the right code no longer works.
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_CLOSED, nb_panel_auth_login(&a, "111111", 1u, 2u, 7u, token));
    NB_CHECK(!nb_panel_auth_is_open(&a, 8u));
}

static void test_a_good_login_resets_the_wrong_count(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 222222u, 0u, 100000u);
    char token[NB_PANEL_TOKEN_LEN + 1];
    for (int i = 0; i < NB_PANEL_MAX_WRONG - 1; i++) {
        nb_panel_auth_login(&a, "000000", 1u, 2u, 5u, token);
    }
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_OK, nb_panel_auth_login(&a, "222222", 3u, 4u, 6u, token));
    NB_CHECK_EQ_INT(0, a.wrong);
}

static void test_malformed_codes_count_as_wrong(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 333333u, 0u, 100000u);
    char token[NB_PANEL_TOKEN_LEN + 1];
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_WRONG, nb_panel_auth_login(&a, NULL, 1u, 2u, 1u, token));
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_WRONG, nb_panel_auth_login(&a, "33333", 1u, 2u, 1u, token));
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_WRONG, nb_panel_auth_login(&a, "3333333", 1u, 2u, 1u, token));
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_WRONG, nb_panel_auth_login(&a, "", 1u, 2u, 1u, token));
}

static void test_use_keeps_the_panel_open_and_idle_closes_it(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 444444u, 0u, 1000u);
    char token[NB_PANEL_TOKEN_LEN + 1];
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_OK, nb_panel_auth_login(&a, "444444", 5u, 6u, 900u, token));
    NB_CHECK(nb_panel_auth_check(&a, token, 1800u));  // used: renewed to 2800
    NB_CHECK(nb_panel_auth_check(&a, token, 2700u));
    NB_CHECK(!nb_panel_auth_check(&a, token, 3700u));  // idle for 1000
    NB_CHECK(!nb_panel_auth_is_open(&a, 3700u));
    NB_CHECK(!nb_panel_auth_check(&a, token, 3701u));
}

static void test_an_unused_code_expires(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 555555u, 0u, 1000u);
    NB_CHECK(nb_panel_auth_is_open(&a, 999u));
    char token[NB_PANEL_TOKEN_LEN + 1];
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_CLOSED, nb_panel_auth_login(&a, "555555", 1u, 2u, 1000u, token));
}

static void test_opening_again_replaces_the_session(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 666666u, 0u, 1000u);
    char old_token[NB_PANEL_TOKEN_LEN + 1];
    nb_panel_auth_login(&a, "666666", 7u, 8u, 1u, old_token);
    nb_panel_auth_open(&a, 777777u, 10u, 1000u);
    NB_CHECK(!nb_panel_auth_check(&a, old_token, 11u));
}

static void test_the_clock_wrap_is_safe(void) {
    nb_panel_auth_t a;
    nb_panel_auth_init(&a);
    nb_panel_auth_open(&a, 888888u, 0xFFFFFF00u, 1000u);  // the deadline wraps past zero
    char token[NB_PANEL_TOKEN_LEN + 1];
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_OK,
                    nb_panel_auth_login(&a, "888888", 1u, 2u, 0xFFFFFFF0u, token));
    NB_CHECK(nb_panel_auth_check(&a, token, 100u));
    NB_CHECK(!nb_panel_auth_is_open(&a, 100u + 1000u));
}

static void test_null_calls_are_safe(void) {
    nb_panel_auth_init(NULL);
    nb_panel_auth_open(NULL, 1u, 0u, 1u);
    nb_panel_auth_close(NULL);
    NB_CHECK(!nb_panel_auth_is_open(NULL, 0u));
    NB_CHECK_EQ_INT(NB_PANEL_LOGIN_CLOSED, nb_panel_auth_login(NULL, "000000", 1u, 2u, 0u, NULL));
    NB_CHECK(!nb_panel_auth_check(NULL, "0000000000000000", 0u));
}

int main(void) {
    NB_RUN(test_the_code_has_six_digits_from_the_random_number);
    NB_RUN(test_the_right_code_gives_a_token_that_checks);
    NB_RUN(test_a_wrong_code_is_refused_and_five_close_the_panel);
    NB_RUN(test_a_good_login_resets_the_wrong_count);
    NB_RUN(test_malformed_codes_count_as_wrong);
    NB_RUN(test_use_keeps_the_panel_open_and_idle_closes_it);
    NB_RUN(test_an_unused_code_expires);
    NB_RUN(test_opening_again_replaces_the_session);
    NB_RUN(test_the_clock_wrap_is_safe);
    NB_RUN(test_null_calls_are_safe);
    NB_TEST_MAIN_END();
}
