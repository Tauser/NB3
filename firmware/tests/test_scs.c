#include "nb_scs.h"
#include "nb_test.h"

// Expected byte strings are worked out by hand from the frame layout
// (FF FF id len instr params checksum, checksum = ~sum(id..last param)).

#define CHECK_BYTES(expected, got, got_len)                                    \
    do {                                                                       \
        NB_CHECK_EQ_INT(sizeof(expected), (got_len));                          \
        NB_CHECK(memcmp((expected), (got), sizeof(expected)) == 0);            \
    } while (0)

static void test_checksum_known_values(void) {
    const uint8_t ping[] = {0x01, 0x02, 0x01};  // sum 4
    NB_CHECK_EQ_INT(0xFB, nb_scs_checksum(ping, sizeof(ping)));
    const uint8_t wrap[] = {0xFE, 0xFE, 0x10};  // sum 0x20C -> low 0x0C
    NB_CHECK_EQ_INT(0xF3, nb_scs_checksum(wrap, sizeof(wrap)));
    NB_CHECK_EQ_INT(0xFF, nb_scs_checksum(ping, 0));
}

static void test_u16_is_big_endian(void) {
    NB_CHECK_EQ_INT(0x0200, nb_scs_decode_u16(0x02, 0x00));
    uint8_t hi = 0, lo = 0;
    nb_scs_encode_u16(0x03FF, &hi, &lo);
    NB_CHECK_EQ_INT(0x03, hi);
    NB_CHECK_EQ_INT(0xFF, lo);
}

static void test_build_ping(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    const uint8_t expected[] = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};
    CHECK_BYTES(expected, buf, nb_scs_build_ping(buf, sizeof(buf), 1));
}

static void test_build_read(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    // sum 01+04+02+38+02 = 0x41
    const uint8_t expected[] = {0xFF, 0xFF, 0x01, 0x04, 0x02, 0x38, 0x02, 0xBE};
    CHECK_BYTES(expected, buf, nb_scs_build_read(buf, sizeof(buf), 1, 0x38, 2));
}

static void test_build_read_feedback(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    // sum 02+04+02+38+08 = 0x48
    const uint8_t expected[] = {0xFF, 0xFF, 0x02, 0x04, 0x02, 0x38, 0x08, 0xB7};
    CHECK_BYTES(expected, buf, nb_scs_build_read_feedback(buf, sizeof(buf), 2));
}

static void test_build_torque(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    const uint8_t on[] = {0xFF, 0xFF, 0x01, 0x04, 0x03, 0x28, 0x01, 0xCE};  // sum 0x31
    CHECK_BYTES(on, buf, nb_scs_build_torque(buf, sizeof(buf), 1, true));
    const uint8_t off[] = {0xFF, 0xFF, 0x01, 0x04, 0x03, 0x28, 0x00, 0xCF};
    CHECK_BYTES(off, buf, nb_scs_build_torque(buf, sizeof(buf), 1, false));
}

static void test_build_goal(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    nb_scs_goal_t g = {.id = 1, .position = 0x0200, .time_ms = 0, .speed = 0x0064};
    // sum 01+09+03+2A+02+00+00+00+00+64 = 0x9D
    const uint8_t expected[] = {0xFF, 0xFF, 0x01, 0x09, 0x03, 0x2A, 0x02, 0x00,
                                0x00, 0x00, 0x00, 0x64, 0x62};
    CHECK_BYTES(expected, buf, nb_scs_build_goal(buf, sizeof(buf), &g));
}

static void test_build_goal_clamps_position(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    nb_scs_goal_t g = {.id = 1, .position = 5000, .time_ms = 0, .speed = 0};
    size_t n = nb_scs_build_goal(buf, sizeof(buf), &g);
    NB_CHECK_EQ_INT(13, n);
    NB_CHECK_EQ_INT(0x03, buf[6]);
    NB_CHECK_EQ_INT(0xFF, buf[7]);
}

static void test_build_sync_goal_two_servos(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    nb_scs_goal_t g[2] = {
        {.id = 1, .position = 0x0200, .time_ms = 20, .speed = 0},
        {.id = 2, .position = 0x0100, .time_ms = 20, .speed = 0},
    };
    // len = (6+1)*2+4 = 0x12; sum low byte 0xF1
    const uint8_t expected[] = {0xFF, 0xFF, 0xFE, 0x12, 0x83, 0x2A, 0x06,
                                0x01, 0x02, 0x00, 0x00, 0x14, 0x00, 0x00,
                                0x02, 0x01, 0x00, 0x00, 0x14, 0x00, 0x00, 0x0E};
    CHECK_BYTES(expected, buf, nb_scs_build_sync_goal(buf, sizeof(buf), g, 2));
}

static void test_build_write_generic(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    const uint8_t data[] = {0xAA, 0xBB};
    // sum 05+05+03+10+AA+BB = 0x182 -> low 0x82 -> 0x7D
    const uint8_t expected[] = {0xFF, 0xFF, 0x05, 0x05, 0x03, 0x10, 0xAA, 0xBB, 0x7D};
    CHECK_BYTES(expected, buf, nb_scs_build_write(buf, sizeof(buf), 5, 0x10, data, 2));
}

static void test_builders_reject_small_buffer(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    memset(buf, 0xAB, sizeof(buf));
    NB_CHECK_EQ_INT(0, nb_scs_build_ping(buf, 5, 1));
    NB_CHECK_EQ_INT(0, nb_scs_build_read(buf, 7, 1, 0x38, 2));
    NB_CHECK_EQ_INT(0, nb_scs_build_torque(buf, 7, 1, true));
    nb_scs_goal_t g = {.id = 1};
    NB_CHECK_EQ_INT(0, nb_scs_build_goal(buf, 12, &g));
    nb_scs_goal_t two[2] = {{.id = 1}, {.id = 2}};
    NB_CHECK_EQ_INT(0, nb_scs_build_sync_goal(buf, 21, two, 2));
    for (size_t i = 0; i < sizeof(buf); i++) {
        NB_CHECK_EQ_INT(0xAB, buf[i]);  // failure leaves the buffer alone
    }
    // Exact fit works.
    NB_CHECK_EQ_INT(6, nb_scs_build_ping(buf, 6, 1));
    NB_CHECK_EQ_INT(22, nb_scs_build_sync_goal(buf, 22, two, 2));
}

static void test_builders_reject_bad_arguments(void) {
    uint8_t buf[NB_SCS_FRAME_MAX];
    const uint8_t data[1] = {0};
    NB_CHECK_EQ_INT(0, nb_scs_build_ping(NULL, 32, 1));
    NB_CHECK_EQ_INT(0, nb_scs_build_ping(buf, sizeof(buf), 0xFF));
    NB_CHECK_EQ_INT(0, nb_scs_build_write(buf, sizeof(buf), 1, 0x28, NULL, 1));
    NB_CHECK_EQ_INT(0, nb_scs_build_goal(buf, sizeof(buf), NULL));
    NB_CHECK_EQ_INT(0, nb_scs_build_sync_goal(buf, sizeof(buf), NULL, 2));
    nb_scs_goal_t g = {.id = 1};
    NB_CHECK_EQ_INT(0, nb_scs_build_sync_goal(buf, sizeof(buf), &g, 0));
    NB_CHECK_EQ_INT(0, nb_scs_build_sync_write(buf, sizeof(buf), 0x2A, 0, data, data, 1));
    // The length field is one byte: 254 data bytes cannot be framed.
    uint8_t big[300];
    uint8_t blob[254] = {0};
    NB_CHECK_EQ_INT(0, nb_scs_build_write(big, sizeof(big), 1, 0, blob, 254));
}

static void test_parse_valid_status(void) {
    // sum 01+04+00+02+00 = 7
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x02, 0x00, 0xF8};
    nb_scs_response_t r;
    size_t used = 0;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK, nb_scs_parse_response(rx, sizeof(rx), 1, &r, &used));
    NB_CHECK_EQ_INT(1, r.id);
    NB_CHECK_EQ_INT(0, r.error);
    NB_CHECK_EQ_INT(2, r.param_len);
    NB_CHECK_EQ_INT(0x0200, nb_scs_decode_u16(r.params[0], r.params[1]));
    NB_CHECK_EQ_INT(sizeof(rx), used);
}

static void test_parse_ping_ack_and_error_byte(void) {
    const uint8_t ack[] = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC};
    nb_scs_response_t r;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK, nb_scs_parse_response(ack, sizeof(ack), 1, &r, NULL));
    NB_CHECK_EQ_INT(0, r.param_len);
    // Overheat + overload reported: sum 01+02+24 = 0x27 -> 0xD8. Still a valid frame.
    const uint8_t err[] = {0xFF, 0xFF, 0x01, 0x02, 0x24, 0xD8};
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK, nb_scs_parse_response(err, sizeof(err), 1, &r, NULL));
    NB_CHECK_EQ_INT(NB_SCS_ERR_OVERHEAT | NB_SCS_ERR_OVERLOAD, r.error);
}

static void test_parse_rejects_bad_checksum(void) {
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x02, 0x00, 0xF9};
    nb_scs_response_t r;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_BAD_CHECKSUM,
                    nb_scs_parse_response(rx, sizeof(rx), 1, &r, NULL));
}

static void test_parse_truncated_is_incomplete(void) {
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x02, 0x00, 0xF8};
    nb_scs_response_t r;
    for (size_t n = 2; n < sizeof(rx); n++) {
        NB_CHECK_EQ_INT(NB_SCS_PARSE_INCOMPLETE, nb_scs_parse_response(rx, n, 1, &r, NULL));
    }
    NB_CHECK_EQ_INT(NB_SCS_PARSE_NO_FRAME, nb_scs_parse_response(rx, 1, 1, &r, NULL));
    NB_CHECK_EQ_INT(NB_SCS_PARSE_NO_FRAME, nb_scs_parse_response(rx, 0, 1, &r, NULL));
}

static void test_parse_skips_leading_garbage(void) {
    const uint8_t rx[] = {0x00, 0xFF, 0x12, 0xFF, 0xFF, 0xFF, 0x01, 0x04,
                          0x00, 0x02, 0x00, 0xF8};
    nb_scs_response_t r;
    size_t used = 0;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK, nb_scs_parse_response(rx, sizeof(rx), 1, &r, &used));
    NB_CHECK_EQ_INT(0x0200, nb_scs_decode_u16(r.params[0], r.params[1]));
    NB_CHECK_EQ_INT(sizeof(rx), used);
}

static void test_parse_pure_garbage(void) {
    const uint8_t rx[] = {0x12, 0x34, 0x56, 0x78, 0x9A};
    nb_scs_response_t r;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_NO_FRAME, nb_scs_parse_response(rx, sizeof(rx), 1, &r, NULL));
    NB_CHECK_EQ_INT(NB_SCS_PARSE_BAD_ARG, nb_scs_parse_response(NULL, 5, 1, &r, NULL));
    NB_CHECK_EQ_INT(NB_SCS_PARSE_BAD_ARG, nb_scs_parse_response(rx, 5, 1, NULL, NULL));
}

static void test_parse_id_filter(void) {
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x02, 0x00, 0xF8};
    nb_scs_response_t r;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_NO_FRAME, nb_scs_parse_response(rx, sizeof(rx), 2, &r, NULL));
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK,
                    nb_scs_parse_response(rx, sizeof(rx), NB_SCS_ID_ANY, &r, NULL));
}

static void test_parse_rejects_oversized_length(void) {
    // len 0x20 claims 30 params: more than any reply carries.
    uint8_t rx[40] = {0xFF, 0xFF, 0x01, 0x20};
    nb_scs_response_t r;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_NO_FRAME, nb_scs_parse_response(rx, sizeof(rx), 1, &r, NULL));
}

static void test_parse_two_frames_reports_first(void) {
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC, 0xFF, 0xFF, 0x02, 0x02, 0x00, 0xFB};
    nb_scs_response_t r;
    size_t used = 0;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK,
                    nb_scs_parse_response(rx, sizeof(rx), NB_SCS_ID_ANY, &r, &used));
    NB_CHECK_EQ_INT(1, r.id);
    NB_CHECK_EQ_INT(6, used);
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK,
                    nb_scs_parse_response(rx + used, sizeof(rx) - used, 2, &r, NULL));
    NB_CHECK_EQ_INT(2, r.id);
}

static void test_echo_prefixed_reply(void) {
    uint8_t tx[NB_SCS_FRAME_MAX];
    size_t tx_len = nb_scs_build_read(tx, sizeof(tx), 1, 0x38, 2);
    const uint8_t reply[] = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x02, 0x00, 0xF8};
    uint8_t rx[NB_SCS_FRAME_MAX * 2];
    memcpy(rx, tx, tx_len);
    memcpy(rx + tx_len, reply, sizeof(reply));
    size_t rx_len = tx_len + sizeof(reply);

    // Unstripped, the echoed request would be taken for a status frame.
    nb_scs_response_t r;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK, nb_scs_parse_response(rx, rx_len, 1, &r, NULL));
    NB_CHECK_EQ_INT(NB_SCS_INSTR_READ, r.error);

    size_t skip = nb_scs_echo_len(rx, rx_len, tx, tx_len);
    NB_CHECK_EQ_INT(tx_len, skip);
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK,
                    nb_scs_parse_response(rx + skip, rx_len - skip, 1, &r, NULL));
    NB_CHECK_EQ_INT(0, r.error);
    NB_CHECK_EQ_INT(0x0200, nb_scs_decode_u16(r.params[0], r.params[1]));
}

static void test_echo_len_cases(void) {
    const uint8_t tx[] = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};
    const uint8_t other[] = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC};
    NB_CHECK_EQ_INT(6, nb_scs_echo_len(tx, 6, tx, 6));
    NB_CHECK_EQ_INT(0, nb_scs_echo_len(tx, 4, tx, 6));     // partial echo
    NB_CHECK_EQ_INT(0, nb_scs_echo_len(other, 6, tx, 6));  // adapter does not echo
    NB_CHECK_EQ_INT(0, nb_scs_echo_len(tx, 6, tx, 0));
    NB_CHECK_EQ_INT(0, nb_scs_echo_len(NULL, 6, tx, 6));
}

static void test_decode_load_sign(void) {
    NB_CHECK_EQ_INT(0, nb_scs_decode_load(0x0000));
    NB_CHECK_EQ_INT(50, nb_scs_decode_load(0x0032));
    NB_CHECK_EQ_INT(-50, nb_scs_decode_load(0x0432));
    NB_CHECK_EQ_INT(1000, nb_scs_decode_load(0x03E8));
    NB_CHECK_EQ_INT(-1000, nb_scs_decode_load(0x07E8));
    NB_CHECK_EQ_INT(0, nb_scs_decode_load(0x0400));  // direction bit with no drive
    NB_CHECK_EQ_INT(5, nb_scs_decode_load(0xF805));  // bits above 10 are ignored
}

static void test_decode_speed_sign(void) {
    NB_CHECK_EQ_INT(5, nb_scs_decode_speed(0x0005));
    NB_CHECK_EQ_INT(-5, nb_scs_decode_speed(0x8005));
    NB_CHECK_EQ_INT(-32767, nb_scs_decode_speed(0xFFFF));
}

static void test_decode_feedback(void) {
    // pos 0x0200, speed -5, load -50, 6.0 V, 30 C; sum 0x122 -> 0xDD
    const uint8_t rx[] = {0xFF, 0xFF, 0x01, 0x0A, 0x00, 0x02, 0x00, 0x80, 0x05,
                          0x04, 0x32, 0x3C, 0x1E, 0xDD};
    nb_scs_response_t r;
    nb_scs_feedback_t fb;
    NB_CHECK_EQ_INT(NB_SCS_PARSE_OK, nb_scs_parse_response(rx, sizeof(rx), 1, &r, NULL));
    NB_CHECK(nb_scs_decode_feedback(&r, &fb));
    NB_CHECK_EQ_INT(512, fb.position);
    NB_CHECK_EQ_INT(-5, fb.speed);
    NB_CHECK_EQ_INT(-50, fb.load);
    NB_CHECK_EQ_INT(60, fb.voltage_dv);
    NB_CHECK_EQ_INT(30, fb.temperature_c);

    r.param_len = 7;
    NB_CHECK(!nb_scs_decode_feedback(&r, &fb));
    NB_CHECK(!nb_scs_decode_feedback(NULL, &fb));
}

static void test_conversions(void) {
    volatile float zero = 0.0f;
    NB_CHECK(nb_scs_counts_to_deg(0) == 0.0f);
    NB_CHECK(nb_scs_counts_to_deg(512) == 160.0f);
    NB_CHECK(nb_scs_counts_to_deg(16) == 5.0f);
    NB_CHECK(nb_scs_counts_to_deg(-16) == -5.0f);
    NB_CHECK_EQ_INT(512, nb_scs_deg_to_counts(160.0f));
    NB_CHECK_EQ_INT(16, nb_scs_deg_to_counts(5.0f));
    NB_CHECK_EQ_INT(17, nb_scs_deg_to_counts(5.2f));      // rounds to nearest
    NB_CHECK_EQ_INT(0, nb_scs_deg_to_counts(-30.0f));     // clamps low
    NB_CHECK_EQ_INT(1023, nb_scs_deg_to_counts(500.0f));  // clamps high
    NB_CHECK_EQ_INT(0, nb_scs_deg_to_counts(zero / zero));  // NaN
    NB_CHECK_EQ_INT(6000, nb_scs_voltage_mv(60));
    NB_CHECK_EQ_INT(0, nb_scs_voltage_mv(0));
    NB_CHECK(nb_scs_load_percent(1000) == 100.0f);
    NB_CHECK(nb_scs_load_percent(-50) == -5.0f);
}

int main(void) {
    NB_RUN(test_checksum_known_values);
    NB_RUN(test_u16_is_big_endian);
    NB_RUN(test_build_ping);
    NB_RUN(test_build_read);
    NB_RUN(test_build_read_feedback);
    NB_RUN(test_build_torque);
    NB_RUN(test_build_goal);
    NB_RUN(test_build_goal_clamps_position);
    NB_RUN(test_build_sync_goal_two_servos);
    NB_RUN(test_build_write_generic);
    NB_RUN(test_builders_reject_small_buffer);
    NB_RUN(test_builders_reject_bad_arguments);
    NB_RUN(test_parse_valid_status);
    NB_RUN(test_parse_ping_ack_and_error_byte);
    NB_RUN(test_parse_rejects_bad_checksum);
    NB_RUN(test_parse_truncated_is_incomplete);
    NB_RUN(test_parse_skips_leading_garbage);
    NB_RUN(test_parse_pure_garbage);
    NB_RUN(test_parse_id_filter);
    NB_RUN(test_parse_rejects_oversized_length);
    NB_RUN(test_parse_two_frames_reports_first);
    NB_RUN(test_echo_prefixed_reply);
    NB_RUN(test_echo_len_cases);
    NB_RUN(test_decode_load_sign);
    NB_RUN(test_decode_speed_sign);
    NB_RUN(test_decode_feedback);
    NB_RUN(test_conversions);
    NB_TEST_MAIN_END();
}
