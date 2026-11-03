#include "nb_uplink_stats.h"
#include "nb_test.h"

static nb_uplink_stats_t started(uint32_t now_ms) {
    nb_uplink_stats_t s;
    memset(&s, 0, sizeof(s));
    nb_uplink_stats_start(&s, now_ms);
    return s;
}

static void test_accounts_for_every_frame(void) {
    nb_uplink_stats_t s = started(1000);
    for (int i = 0; i < 10; i++) nb_uplink_stats_on_pushed(&s);
    for (int i = 0; i < 3; i++) nb_uplink_stats_on_dropped(&s);
    for (int i = 0; i < 6; i++) nb_uplink_stats_on_encoded(&s, 100);
    nb_uplink_stats_on_send_dropped(&s);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 1600, &r));
    NB_CHECK_EQ_INT(600, r.duration_ms);
    NB_CHECK_EQ_INT(10, r.pushed);
    NB_CHECK_EQ_INT(3, r.dropped);
    NB_CHECK_EQ_INT(6, r.encoded);
    NB_CHECK_EQ_INT(1, r.pending);
    NB_CHECK_EQ_INT(1, r.send_dropped);
    NB_CHECK_EQ_INT(600, r.opus_bytes);
}

static void test_frame_from_before_the_session_does_not_underflow(void) {
    nb_uplink_stats_t s = started(0);
    nb_uplink_stats_on_encoded(&s, 100);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 60, &r));
    NB_CHECK_EQ_INT(0, r.pending);
}

static void test_small_packets_are_the_silent_ones(void) {
    nb_uplink_stats_t s = started(0);
    nb_uplink_stats_on_encoded(&s, NB_UPLINK_SMALL_PACKET_BYTES);
    nb_uplink_stats_on_encoded(&s, NB_UPLINK_SMALL_PACKET_BYTES + 1);
    nb_uplink_stats_on_encoded(&s, 1);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 180, &r));
    NB_CHECK_EQ_INT(2, r.small_packets);
    NB_CHECK_EQ_INT(3, r.encoded);
}

static void test_voice_runs_and_time(void) {
    nb_uplink_stats_t s = started(0);
    nb_uplink_stats_on_voice(&s, true, 100);
    nb_uplink_stats_on_voice(&s, true, 150);  // same state: not a new run
    nb_uplink_stats_on_voice(&s, false, 400);
    nb_uplink_stats_on_voice(&s, false, 450);
    nb_uplink_stats_on_voice(&s, true, 500);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 700, &r));  // closes the open run
    NB_CHECK_EQ_INT(2, r.voice_runs);
    NB_CHECK_EQ_INT(500, r.voice_ms);
}

static void test_restart_while_active_keeps_the_session(void) {
    nb_uplink_stats_t s = started(100);
    nb_uplink_stats_on_pushed(&s);
    nb_uplink_stats_start(&s, 500);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 700, &r));
    NB_CHECK_EQ_INT(600, r.duration_ms);
    NB_CHECK_EQ_INT(1, r.pushed);
}

static void test_finish_reports_once_and_ignores_events_after(void) {
    nb_uplink_stats_t s = started(0);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 10, &r));
    NB_CHECK(!nb_uplink_stats_finish(&s, 20, &r));
    nb_uplink_stats_on_pushed(&s);
    NB_CHECK_EQ_INT(0, s.pushed);
}

static void test_never_started_reports_nothing(void) {
    nb_uplink_stats_t s;
    memset(&s, 0, sizeof(s));
    nb_uplink_stats_on_pushed(&s);
    nb_uplink_report_t r;
    NB_CHECK(!nb_uplink_stats_finish(&s, 10, &r));
}

static void test_clock_wrap(void) {
    nb_uplink_stats_t s = started(0xFFFFFF00u);
    nb_uplink_stats_on_voice(&s, true, 0xFFFFFF80u);
    nb_uplink_stats_on_voice(&s, false, 0x00000080u);
    nb_uplink_report_t r;
    NB_CHECK(nb_uplink_stats_finish(&s, 0x00000100u, &r));
    NB_CHECK_EQ_INT(0x200, r.duration_ms);
    NB_CHECK_EQ_INT(0x100, r.voice_ms);
}

static void test_null_safe(void) {
    nb_uplink_report_t r;
    nb_uplink_stats_start(NULL, 0);
    nb_uplink_stats_on_pushed(NULL);
    nb_uplink_stats_on_dropped(NULL);
    nb_uplink_stats_on_encoded(NULL, 1);
    nb_uplink_stats_on_send_dropped(NULL);
    nb_uplink_stats_on_voice(NULL, true, 0);
    NB_CHECK(!nb_uplink_stats_finish(NULL, 0, &r));
    nb_uplink_tap_header(NULL, NB_UPLINK_TAP_PACKET, 0, 0);
}

static void test_tap_header_layout(void) {
    uint8_t h[NB_UPLINK_TAP_HEADER_BYTES];
    nb_uplink_tap_header(h, NB_UPLINK_TAP_TURN_END, 0x01020304u, 0xA0B0C0D0u);
    const uint8_t expected[NB_UPLINK_TAP_HEADER_BYTES] = {
        'N', 'B', 'U', 'P', 2, 0, 0, 0, 0x01, 0x02, 0x03, 0x04, 0xA0, 0xB0, 0xC0, 0xD0};
    NB_CHECK(memcmp(h, expected, sizeof(expected)) == 0);
}

int main(void) {
    NB_RUN(test_accounts_for_every_frame);
    NB_RUN(test_frame_from_before_the_session_does_not_underflow);
    NB_RUN(test_small_packets_are_the_silent_ones);
    NB_RUN(test_voice_runs_and_time);
    NB_RUN(test_restart_while_active_keeps_the_session);
    NB_RUN(test_finish_reports_once_and_ignores_events_after);
    NB_RUN(test_never_started_reports_nothing);
    NB_RUN(test_clock_wrap);
    NB_RUN(test_null_safe);
    NB_RUN(test_tap_header_layout);
    NB_TEST_MAIN_END();
}
