#include <string.h>

#include "nb_store.h"
#include "nb_test.h"

static const char kPayload[] = "perfil: Tauser; fatos: gosta de cafe; versao 1";

static size_t make_slot(uint32_t gen, uint32_t max_seq, uint8_t *out, size_t cap) {
    return nb_slot_encode(gen, max_seq, kPayload, (uint32_t)sizeof(kPayload), out, cap);
}

static void test_crc32_matches_the_standard(void) {
    NB_CHECK_EQ_INT(0xCBF43926u, nb_store_crc32(0, "123456789", 9));
    // Chained calls equal one call over the whole buffer.
    NB_CHECK_EQ_INT(nb_store_crc32(0, "123456789", 9),
                    nb_store_crc32(nb_store_crc32(0, "1234", 4), "56789", 5));
}

static void test_slot_round_trip(void) {
    uint8_t buf[256];
    const size_t n = make_slot(7, 42, buf, sizeof(buf));
    NB_CHECK_EQ_INT(NB_STORE_SLOT_HEADER_SIZE + sizeof(kPayload), n);
    nb_slot_info_t info;
    const uint8_t *payload = NULL;
    NB_CHECK(nb_slot_decode(buf, n, &info, &payload));
    NB_CHECK_EQ_INT(7, info.generation);
    NB_CHECK_EQ_INT(42, info.max_seq);
    NB_CHECK_EQ_INT(sizeof(kPayload), info.length);
    NB_CHECK(memcmp(payload, kPayload, sizeof(kPayload)) == 0);
    NB_CHECK_EQ_INT(0, make_slot(1, 0, buf, 10));  // does not fit
}

static void test_every_flipped_byte_invalidates_a_slot(void) {
    uint8_t good[256], bad[256];
    const size_t n = make_slot(3, 9, good, sizeof(good));
    for (size_t i = 0; i < n; i++) {
        for (int bit = 0; bit < 8; bit++) {
            memcpy(bad, good, n);
            bad[i] ^= (uint8_t)(1u << bit);
            if (nb_slot_decode(bad, n, NULL, NULL)) {
                fprintf(stderr, "flip at byte %u bit %d accepted\n", (unsigned)i, bit);
                NB_CHECK(false);
            }
        }
    }
}

static void test_every_truncation_invalidates_a_slot(void) {
    uint8_t buf[300];
    const size_t n = make_slot(3, 9, buf, sizeof(buf));
    for (size_t cut = 0; cut < n; cut++) {
        NB_CHECK(!nb_slot_decode(buf, cut, NULL, NULL));
    }
    buf[n] = 0;  // trailing junk: the size no longer matches the header
    NB_CHECK(!nb_slot_decode(buf, n + 1, NULL, NULL));
}

static void test_damaged_newer_slot_never_wins(void) {
    uint8_t a[256], b[256], damaged[256];
    const size_t na = make_slot(5, 0, a, sizeof(a));
    const size_t nb = make_slot(6, 0, b, sizeof(b));
    nb_slot_info_t ia;
    NB_CHECK(nb_slot_decode(a, na, &ia, NULL));
    for (size_t i = 0; i < nb; i++) {
        memcpy(damaged, b, nb);
        damaged[i] ^= 0xFF;
        nb_slot_info_t ib;
        const bool b_ok = nb_slot_decode(damaged, nb, &ib, NULL);
        const nb_slot_choice_t c = nb_slot_choose(true, &ia, b_ok, &ib);
        NB_CHECK(!b_ok);
        NB_CHECK_EQ_INT(0, c.load);   // the older, intact slot
        NB_CHECK_EQ_INT(1, c.write);  // the damaged one is overwritten
        NB_CHECK_EQ_INT(6, c.next_gen);
    }
}

static void test_damaged_older_slot_is_not_promoted(void) {
    // A torn generation field must not make the older slot look newer.
    uint8_t a[256], b[256];
    const size_t na = make_slot(5, 0, a, sizeof(a));
    const size_t nb = make_slot(6, 0, b, sizeof(b));
    a[8] = 0x99;  // generation of A becomes 0x99 without a matching CRC
    nb_slot_info_t ia, ib;
    const bool a_ok = nb_slot_decode(a, na, &ia, NULL);
    NB_CHECK(!a_ok);
    NB_CHECK(nb_slot_decode(b, nb, &ib, NULL));
    NB_CHECK_EQ_INT(1, nb_slot_choose(a_ok, &ia, true, &ib).load);
}

static void test_slots_alternate_and_never_overwrite_the_only_valid_one(void) {
    nb_slot_choice_t c = nb_slot_choose(false, NULL, false, NULL);
    NB_CHECK_EQ_INT(-1, c.load);
    NB_CHECK_EQ_INT(0, c.write);
    NB_CHECK_EQ_INT(1, c.next_gen);

    nb_slot_info_t a = {1, 0, 0}, b = {2, 0, 0};
    c = nb_slot_choose(true, &a, false, NULL);
    NB_CHECK_EQ_INT(0, c.load);
    NB_CHECK_EQ_INT(1, c.write);
    NB_CHECK_EQ_INT(2, c.next_gen);
    c = nb_slot_choose(true, &a, true, &b);
    NB_CHECK_EQ_INT(1, c.load);
    NB_CHECK_EQ_INT(0, c.write);
    NB_CHECK_EQ_INT(3, c.next_gen);
    c = nb_slot_choose(false, NULL, true, &b);
    NB_CHECK_EQ_INT(1, c.load);
    NB_CHECK_EQ_INT(0, c.write);
}

// A log of five frames with payloads of different lengths.
static size_t make_log(uint8_t *buf, size_t cap, size_t ends[5]) {
    size_t at = 0;
    for (uint32_t i = 0; i < 5; i++) {
        char payload[32];
        const int len = snprintf(payload, sizeof(payload), "episodio %u %.*s", (unsigned)i,
                                 (int)i * 3, "xxxxxxxxxxxxxxx");
        at += nb_frame_encode(10 + i, payload, (uint32_t)len, buf + at, cap - at);
        ends[i] = at;
    }
    return at;
}

typedef struct {
    uint32_t seqs[8];
    int count;
} visits_t;

static bool record(uint32_t seq, const uint8_t *payload, uint32_t length, void *ctx) {
    (void)payload;
    (void)length;
    visits_t *v = (visits_t *)ctx;
    v->seqs[v->count++] = seq;
    return true;
}

static void test_log_round_trip(void) {
    uint8_t buf[512];
    size_t ends[5];
    const size_t n = make_log(buf, sizeof(buf), ends);
    visits_t v = {{0}, 0};
    nb_log_scan_t r = nb_log_scan(buf, n, record, &v);
    NB_CHECK_EQ_INT(n, r.valid_bytes);
    NB_CHECK_EQ_INT(5, r.frames);
    NB_CHECK_EQ_INT(14, r.last_seq);
    NB_CHECK_EQ_INT(5, v.count);
    NB_CHECK_EQ_INT(10, v.seqs[0]);
    NB_CHECK_EQ_INT(14, v.seqs[4]);
}

static size_t whole_frames_before(const size_t ends[5], size_t limit, uint32_t *frames) {
    size_t good = 0;
    *frames = 0;
    for (int i = 0; i < 5; i++) {
        if (ends[i] <= limit) {
            good = ends[i];
            (*frames)++;
        }
    }
    return good;
}

static void test_every_truncation_keeps_the_whole_frames(void) {
    uint8_t buf[512];
    size_t ends[5];
    const size_t n = make_log(buf, sizeof(buf), ends);
    for (size_t cut = 0; cut <= n; cut++) {
        uint32_t frames;
        const size_t expect = whole_frames_before(ends, cut, &frames);
        const nb_log_scan_t r = nb_log_scan(buf, cut, NULL, NULL);
        NB_CHECK_EQ_INT(expect, r.valid_bytes);
        NB_CHECK_EQ_INT(frames, r.frames);
    }
}

static void test_every_flipped_byte_stops_at_its_frame(void) {
    uint8_t good[512], bad[512];
    size_t ends[5];
    const size_t n = make_log(good, sizeof(good), ends);
    for (size_t i = 0; i < n; i++) {
        memcpy(bad, good, n);
        bad[i] ^= 0x5A;
        // The frame holding byte i and everything after it are dropped.
        size_t start = 0;
        uint32_t frames = 0;
        for (int f = 0; f < 5; f++) {
            if (ends[f] <= i) {
                start = ends[f];
                frames++;
            }
        }
        visits_t v = {{0}, 0};
        const nb_log_scan_t r = nb_log_scan(bad, n, record, &v);
        if (r.valid_bytes != start) {
            fprintf(stderr, "flip at byte %u: valid %u, expected %u\n", (unsigned)i,
                    (unsigned)r.valid_bytes, (unsigned)start);
        }
        NB_CHECK_EQ_INT(start, r.valid_bytes);
        NB_CHECK_EQ_INT(frames, r.frames);
        NB_CHECK_EQ_INT(frames, v.count);
    }
}

static bool stop_after_two(uint32_t seq, const uint8_t *payload, uint32_t length, void *ctx) {
    (void)seq;
    (void)payload;
    (void)length;
    return ++*(int *)ctx < 2;
}

static void test_visitor_can_stop_early(void) {
    uint8_t buf[512];
    size_t ends[5];
    const size_t n = make_log(buf, sizeof(buf), ends);
    int seen = 0;
    const nb_log_scan_t r = nb_log_scan(buf, n, stop_after_two, &seen);
    NB_CHECK_EQ_INT(2, seen);
    NB_CHECK_EQ_INT(2, r.frames);
    NB_CHECK_EQ_INT(ends[1], r.valid_bytes);
}

static void test_garbage_and_null(void) {
    uint8_t junk[100];
    memset(junk, 0xA5, sizeof(junk));
    NB_CHECK_EQ_INT(0, nb_log_scan(junk, sizeof(junk), NULL, NULL).valid_bytes);
    NB_CHECK_EQ_INT(0, nb_log_scan(NULL, 10, NULL, NULL).valid_bytes);
    NB_CHECK(!nb_slot_decode(junk, sizeof(junk), NULL, NULL));
    NB_CHECK(!nb_slot_decode(NULL, 0, NULL, NULL));
    NB_CHECK_EQ_INT(0, nb_frame_encode(1, NULL, 5, junk, sizeof(junk)));
    NB_CHECK_EQ_INT(0, nb_slot_encode(1, 0, NULL, 5, junk, sizeof(junk)));
}

int main(void) {
    NB_RUN(test_crc32_matches_the_standard);
    NB_RUN(test_slot_round_trip);
    NB_RUN(test_every_flipped_byte_invalidates_a_slot);
    NB_RUN(test_every_truncation_invalidates_a_slot);
    NB_RUN(test_damaged_newer_slot_never_wins);
    NB_RUN(test_damaged_older_slot_is_not_promoted);
    NB_RUN(test_slots_alternate_and_never_overwrite_the_only_valid_one);
    NB_RUN(test_log_round_trip);
    NB_RUN(test_every_truncation_keeps_the_whole_frames);
    NB_RUN(test_every_flipped_byte_stops_at_its_frame);
    NB_RUN(test_visitor_can_stop_early);
    NB_RUN(test_garbage_and_null);
    NB_TEST_MAIN_END();
}
