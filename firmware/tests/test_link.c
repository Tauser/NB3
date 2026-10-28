#include "nb_link.h"
#include "nb_test.h"

static void test_boot_waits_then_comes_online(void) {
    nb_link_t l;
    nb_link_init(&l, 0);
    NB_CHECK(!nb_link_update(&l, false, false, false, 10000));  // no Wi-Fi yet: not "lost"
    NB_CHECK_EQ_INT(NB_LINK_STARTING, l.state);
    NB_CHECK(!nb_link_online(&l));
    NB_CHECK(nb_link_update(&l, true, false, false, 12000));
    NB_CHECK_EQ_INT(NB_LINK_ONLINE, l.state);
}

static void test_a_short_blip_is_not_a_loss(void) {
    nb_link_t l;
    nb_link_init(&l, 0);
    nb_link_update(&l, true, false, false, 1000);
    NB_CHECK(!nb_link_update(&l, false, false, false, 2000));
    NB_CHECK(!nb_link_update(&l, false, false, false, 6999));
    NB_CHECK(!nb_link_update(&l, true, false, false, 7000));
    NB_CHECK_EQ_INT(NB_LINK_ONLINE, l.state);
    NB_CHECK(!nb_link_update(&l, false, false, false, 8000));
    NB_CHECK(nb_link_update(&l, false, false, false, 13000));  // 5 s down
    NB_CHECK_EQ_INT(NB_LINK_NO_WIFI, l.state);
    NB_CHECK_EQ_INT(13000, l.since_ms);
    NB_CHECK(nb_link_update(&l, true, false, false, 20000));  // back
    NB_CHECK_EQ_INT(NB_LINK_ONLINE, l.state);
}

static void test_a_server_error_lasts_until_a_conversation_works(void) {
    nb_link_t l;
    nb_link_init(&l, 0);
    nb_link_update(&l, true, false, false, 1000);
    NB_CHECK(nb_link_update(&l, true, false, true, 2000));
    NB_CHECK_EQ_INT(NB_LINK_NO_SERVER, l.state);
    NB_CHECK(!nb_link_update(&l, true, false, false, 60000));  // quiet: still no server
    NB_CHECK(nb_link_update(&l, true, true, false, 61000));
    NB_CHECK_EQ_INT(NB_LINK_ONLINE, l.state);
    // Both in one sample: the conversation that worked wins.
    nb_link_update(&l, true, false, true, 62000);
    NB_CHECK(nb_link_update(&l, true, true, true, 63000));
    NB_CHECK_EQ_INT(NB_LINK_ONLINE, l.state);
}

static void test_wifi_back_gives_the_server_a_new_chance(void) {
    nb_link_t l;
    nb_link_init(&l, 0);
    nb_link_update(&l, true, false, false, 1000);
    nb_link_update(&l, true, false, true, 2000);
    nb_link_update(&l, false, false, false, 3000);
    NB_CHECK(nb_link_update(&l, false, false, false, 9000));
    NB_CHECK_EQ_INT(NB_LINK_NO_WIFI, l.state);
    // Without Wi-Fi a server error says nothing new.
    NB_CHECK(!nb_link_update(&l, false, false, true, 9500));
    NB_CHECK(nb_link_update(&l, true, false, false, 10000));
    NB_CHECK_EQ_INT(NB_LINK_ONLINE, l.state);
}

static void test_clock_wrap_is_safe(void) {
    nb_link_t l;
    nb_link_init(&l, 0xFFFFF000u);
    nb_link_update(&l, true, false, false, 0xFFFFF100u);
    nb_link_update(&l, false, false, false, 0xFFFFF200u);
    NB_CHECK(!nb_link_update(&l, false, false, false, 0x00000100u));  // 3.8 s later
    NB_CHECK(nb_link_update(&l, false, false, false, 0x00001100u));   // 7.9 s later
    NB_CHECK_EQ_STR("no_wifi", nb_link_state_name(l.state));
}

int main(void) {
    NB_RUN(test_boot_waits_then_comes_online);
    NB_RUN(test_a_short_blip_is_not_a_loss);
    NB_RUN(test_a_server_error_lasts_until_a_conversation_works);
    NB_RUN(test_wifi_back_gives_the_server_a_new_chance);
    NB_RUN(test_clock_wrap_is_safe);
    NB_TEST_MAIN_END();
}
