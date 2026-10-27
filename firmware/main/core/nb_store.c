#include "nb_store.h"

#include <string.h>

// Little-endian on the card, whatever the CPU.
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint32_t nb_store_crc32(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

// Slot header: magic, version(16) + reserved(16), generation, max_seq,
// length, crc. The CRC covers the first 20 bytes and the payload.
size_t nb_slot_encode(uint32_t generation, uint32_t max_seq, const void *payload,
                      uint32_t length, uint8_t *out, size_t out_cap) {
    const size_t total = NB_STORE_SLOT_HEADER_SIZE + (size_t)length;
    if (out == NULL || length > NB_STORE_MAX_PAYLOAD || total > out_cap ||
        (length > 0 && payload == NULL)) {
        return 0;
    }
    put32(out, NB_STORE_SLOT_MAGIC);
    put32(out + 4, NB_STORE_FORMAT_VERSION);
    put32(out + 8, generation);
    put32(out + 12, max_seq);
    put32(out + 16, length);
    if (length > 0) {
        memcpy(out + NB_STORE_SLOT_HEADER_SIZE, payload, length);
    }
    uint32_t crc = nb_store_crc32(0, out, 20);
    crc = nb_store_crc32(crc, out + NB_STORE_SLOT_HEADER_SIZE, length);
    put32(out + 20, crc);
    return total;
}

bool nb_slot_decode(const uint8_t *buf, size_t size, nb_slot_info_t *info,
                    const uint8_t **payload) {
    if (buf == NULL || size < NB_STORE_SLOT_HEADER_SIZE || get32(buf) != NB_STORE_SLOT_MAGIC ||
        get32(buf + 4) != NB_STORE_FORMAT_VERSION) {
        return false;
    }
    const uint32_t length = get32(buf + 16);
    if (length > NB_STORE_MAX_PAYLOAD || size != NB_STORE_SLOT_HEADER_SIZE + (size_t)length) {
        return false;
    }
    uint32_t crc = nb_store_crc32(0, buf, 20);
    crc = nb_store_crc32(crc, buf + NB_STORE_SLOT_HEADER_SIZE, length);
    if (crc != get32(buf + 20)) {
        return false;
    }
    if (info != NULL) {
        info->generation = get32(buf + 8);
        info->max_seq = get32(buf + 12);
        info->length = length;
    }
    if (payload != NULL) {
        *payload = buf + NB_STORE_SLOT_HEADER_SIZE;
    }
    return true;
}

size_t nb_slot_size(const uint8_t *header, size_t size) {
    if (header == NULL || size < NB_STORE_SLOT_HEADER_SIZE ||
        get32(header) != NB_STORE_SLOT_MAGIC || get32(header + 4) != NB_STORE_FORMAT_VERSION) {
        return 0;
    }
    const uint32_t length = get32(header + 16);
    return length > NB_STORE_MAX_PAYLOAD ? 0 : NB_STORE_SLOT_HEADER_SIZE + (size_t)length;
}

nb_slot_choice_t nb_slot_choose(bool a_valid, const nb_slot_info_t *a, bool b_valid,
                                const nb_slot_info_t *b) {
    nb_slot_choice_t c = {-1, 0, 1};
    a_valid = a_valid && a != NULL;
    b_valid = b_valid && b != NULL;
    if (a_valid && b_valid) {
        const bool a_newer = a->generation >= b->generation;
        c.load = a_newer ? 0 : 1;
        c.write = a_newer ? 1 : 0;
        c.next_gen = (a_newer ? a->generation : b->generation) + 1;
    } else if (a_valid) {
        c.load = 0;
        c.write = 1;
        c.next_gen = a->generation + 1;
    } else if (b_valid) {
        c.load = 1;
        c.write = 0;
        c.next_gen = b->generation + 1;
    }
    return c;
}

// Frame: magic, length, seq, payload, crc over length + seq + payload.
size_t nb_frame_size(uint32_t length) {
    return NB_STORE_FRAME_OVERHEAD + (size_t)length;
}

size_t nb_frame_encode(uint32_t seq, const void *payload, uint32_t length, uint8_t *out,
                       size_t out_cap) {
    const size_t total = nb_frame_size(length);
    if (out == NULL || length > NB_STORE_MAX_PAYLOAD || total > out_cap ||
        (length > 0 && payload == NULL)) {
        return 0;
    }
    put32(out, NB_STORE_FRAME_MAGIC);
    put32(out + 4, length);
    put32(out + 8, seq);
    if (length > 0) {
        memcpy(out + 12, payload, length);
    }
    put32(out + 12 + length, nb_store_crc32(0, out + 4, 8 + (size_t)length));
    return total;
}

size_t nb_log_entry_size(uint32_t length) {
    return nb_frame_size(length) + NB_STORE_LOG_END;
}

size_t nb_log_entry_encode(uint32_t seq, const void *payload, uint32_t length, uint8_t *out,
                           size_t out_cap) {
    if (out_cap < nb_log_entry_size(length)) {
        return 0;
    }
    const size_t n = nb_frame_encode(seq, payload, length, out, out_cap);
    if (n == 0) {
        return 0;
    }
    memset(out + n, 0, NB_STORE_LOG_END);
    return n + NB_STORE_LOG_END;
}

nb_log_scan_t nb_log_scan(const uint8_t *buf, size_t size, nb_frame_visit_t visit, void *ctx) {
    nb_log_scan_t r = {0, 0, 0};
    if (buf == NULL) {
        return r;
    }
    size_t at = 0;
    while (size - at >= NB_STORE_FRAME_OVERHEAD) {
        const uint8_t *f = buf + at;
        const uint32_t length = get32(f + 4);
        if (get32(f) != NB_STORE_FRAME_MAGIC || length > NB_STORE_MAX_PAYLOAD ||
            nb_frame_size(length) > size - at) {
            break;
        }
        const uint32_t seq = get32(f + 8);
        if (nb_store_crc32(0, f + 4, 8 + (size_t)length) != get32(f + 12 + length)) {
            break;
        }
        at += nb_frame_size(length);
        r.valid_bytes = at;
        r.frames++;
        r.last_seq = seq;
        if (visit != NULL && !visit(seq, f + 12, length, ctx)) {
            break;
        }
    }
    return r;
}
