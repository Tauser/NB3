#include "nb_scs.h"

#include <math.h>
#include <string.h>

#define FRAME_OVERHEAD 6u          // FF FF id len instr checksum
#define MAX_PARAMS_BY_LEN_FIELD 253u  // len = params + 2 must fit one byte

// Degrees per count is 5/16 (the 0..1023 range covers ~320 degrees).
#define DEG_PER_COUNT 0.3125f

uint8_t nb_scs_checksum(const uint8_t *bytes, size_t len) {
    uint32_t sum = 0;
    if (bytes == NULL) {
        return 0xFFu;
    }
    for (size_t i = 0; i < len; i++) {
        sum += bytes[i];
    }
    return (uint8_t)(~sum & 0xFFu);
}

uint16_t nb_scs_decode_u16(uint8_t high, uint8_t low) {
    return (uint16_t)(((uint16_t)high << 8) | low);
}

void nb_scs_encode_u16(uint16_t value, uint8_t *high, uint8_t *low) {
    if (high != NULL) {
        *high = (uint8_t)(value >> 8);
    }
    if (low != NULL) {
        *low = (uint8_t)(value & 0xFFu);
    }
}

// Checks arguments and capacity, then writes the 5-byte frame head. Returns
// the total frame size the caller must fill (params at out + 5, then
// frame_finish), or 0 on failure.
static size_t frame_begin(uint8_t *out, size_t cap, uint8_t id, uint8_t instr, size_t params) {
    if (out == NULL || id == NB_SCS_ID_ANY || params > MAX_PARAMS_BY_LEN_FIELD) {
        return 0;
    }
    size_t total = FRAME_OVERHEAD + params;
    if (cap < total) {
        return 0;
    }
    out[0] = NB_SCS_HEADER;
    out[1] = NB_SCS_HEADER;
    out[2] = id;
    out[3] = (uint8_t)(params + 2u);
    out[4] = instr;
    return total;
}

static size_t frame_finish(uint8_t *out, size_t total) {
    out[total - 1] = nb_scs_checksum(&out[2], total - 3);
    return total;
}

size_t nb_scs_build_ping(uint8_t *out, size_t cap, uint8_t id) {
    size_t total = frame_begin(out, cap, id, NB_SCS_INSTR_PING, 0);
    return total ? frame_finish(out, total) : 0;
}

size_t nb_scs_build_read(uint8_t *out, size_t cap, uint8_t id, uint8_t addr, uint8_t count) {
    size_t total = frame_begin(out, cap, id, NB_SCS_INSTR_READ, 2);
    if (!total) {
        return 0;
    }
    out[5] = addr;
    out[6] = count;
    return frame_finish(out, total);
}

size_t nb_scs_build_write(uint8_t *out, size_t cap, uint8_t id, uint8_t addr,
                          const uint8_t *data, uint8_t data_len) {
    if (data_len > 0 && data == NULL) {
        return 0;
    }
    size_t total = frame_begin(out, cap, id, NB_SCS_INSTR_WRITE, 1u + data_len);
    if (!total) {
        return 0;
    }
    out[5] = addr;
    if (data_len > 0) {
        memcpy(&out[6], data, data_len);
    }
    return frame_finish(out, total);
}

size_t nb_scs_build_sync_write(uint8_t *out, size_t cap, uint8_t addr, uint8_t data_len,
                               const uint8_t *ids, const uint8_t *data, uint8_t count) {
    if (ids == NULL || data == NULL || count == 0 || data_len == 0) {
        return 0;
    }
    size_t params = 2u + (size_t)count * (1u + data_len);
    size_t total = frame_begin(out, cap, NB_SCS_ID_BROADCAST, NB_SCS_INSTR_SYNC_WRITE, params);
    if (!total) {
        return 0;
    }
    size_t pos = 5;
    out[pos++] = addr;
    out[pos++] = data_len;
    for (uint8_t i = 0; i < count; i++) {
        out[pos++] = ids[i];
        memcpy(&out[pos], &data[(size_t)i * data_len], data_len);
        pos += data_len;
    }
    return frame_finish(out, total);
}

size_t nb_scs_build_torque(uint8_t *out, size_t cap, uint8_t id, bool enable) {
    uint8_t value = enable ? 1u : 0u;
    return nb_scs_build_write(out, cap, id, NB_SCS_REG_TORQUE_ENABLE, &value, 1);
}

static void pack_goal(uint8_t block[NB_SCS_GOAL_BLOCK_LEN], const nb_scs_goal_t *goal) {
    uint16_t position = goal->position > NB_SCS_POSITION_MAX ? NB_SCS_POSITION_MAX
                                                             : goal->position;
    nb_scs_encode_u16(position, &block[0], &block[1]);
    nb_scs_encode_u16(goal->time_ms, &block[2], &block[3]);
    nb_scs_encode_u16(goal->speed, &block[4], &block[5]);
}

size_t nb_scs_build_goal(uint8_t *out, size_t cap, const nb_scs_goal_t *goal) {
    uint8_t block[NB_SCS_GOAL_BLOCK_LEN];
    if (goal == NULL) {
        return 0;
    }
    pack_goal(block, goal);
    return nb_scs_build_write(out, cap, goal->id, NB_SCS_REG_GOAL_POSITION, block,
                              NB_SCS_GOAL_BLOCK_LEN);
}

size_t nb_scs_build_sync_goal(uint8_t *out, size_t cap, const nb_scs_goal_t *goals,
                              uint8_t count) {
    // Two servos on the bus; the cap keeps the staging arrays on the stack.
    enum { MAX_SYNC_SERVOS = 8 };
    uint8_t ids[MAX_SYNC_SERVOS];
    uint8_t blocks[MAX_SYNC_SERVOS * NB_SCS_GOAL_BLOCK_LEN];
    if (goals == NULL || count == 0 || count > MAX_SYNC_SERVOS) {
        return 0;
    }
    for (uint8_t i = 0; i < count; i++) {
        ids[i] = goals[i].id;
        pack_goal(&blocks[(size_t)i * NB_SCS_GOAL_BLOCK_LEN], &goals[i]);
    }
    return nb_scs_build_sync_write(out, cap, NB_SCS_REG_GOAL_POSITION, NB_SCS_GOAL_BLOCK_LEN,
                                   ids, blocks, count);
}

size_t nb_scs_build_read_feedback(uint8_t *out, size_t cap, uint8_t id) {
    return nb_scs_build_read(out, cap, id, NB_SCS_REG_PRESENT_POSITION, NB_SCS_FEEDBACK_LEN);
}

size_t nb_scs_echo_len(const uint8_t *rx, size_t rx_len, const uint8_t *tx, size_t tx_len) {
    if (rx == NULL || tx == NULL || tx_len == 0 || rx_len < tx_len) {
        return 0;
    }
    return memcmp(rx, tx, tx_len) == 0 ? tx_len : 0;
}

nb_scs_parse_status_t nb_scs_parse_response(const uint8_t *rx, size_t len, uint8_t expect_id,
                                            nb_scs_response_t *out, size_t *consumed) {
    bool incomplete = false;
    bool bad_checksum = false;

    if (rx == NULL || out == NULL) {
        return NB_SCS_PARSE_BAD_ARG;
    }
    for (size_t i = 0; i + 1 < len; i++) {
        if (rx[i] != NB_SCS_HEADER || rx[i + 1] != NB_SCS_HEADER) {
            continue;
        }
        if (i + 4 > len) {  // header seen but id/len not yet arrived
            incomplete = true;
            continue;
        }
        uint8_t id = rx[i + 2];
        uint8_t flen = rx[i + 3];
        // FF FF FF is a header followed by another header byte, not an id.
        if (id == NB_SCS_ID_ANY || flen < 2u ||
            (size_t)(flen - 2u) > NB_SCS_RESPONSE_MAX_PARAMS) {
            continue;
        }
        size_t total = 4u + flen;
        if (i + total > len) {
            incomplete = true;
            continue;
        }
        if (nb_scs_checksum(&rx[i + 2], total - 3) != rx[i + total - 1]) {
            bad_checksum = true;
            continue;
        }
        if (expect_id != NB_SCS_ID_ANY && id != expect_id) {
            continue;
        }
        out->id = id;
        out->error = rx[i + 4];
        out->param_len = (uint8_t)(flen - 2u);
        if (out->param_len > 0) {
            memcpy(out->params, &rx[i + 5], out->param_len);
        }
        if (consumed != NULL) {
            *consumed = i + total;
        }
        return NB_SCS_PARSE_OK;
    }
    if (incomplete) {
        return NB_SCS_PARSE_INCOMPLETE;
    }
    return bad_checksum ? NB_SCS_PARSE_BAD_CHECKSUM : NB_SCS_PARSE_NO_FRAME;
}

int16_t nb_scs_decode_load(uint16_t raw) {
    int16_t magnitude = (int16_t)(raw & 0x03FFu);
    return (raw & 0x0400u) ? (int16_t)-magnitude : magnitude;
}

int16_t nb_scs_decode_speed(uint16_t raw) {
    int16_t magnitude = (int16_t)(raw & 0x7FFFu);
    return (raw & 0x8000u) ? (int16_t)-magnitude : magnitude;
}

bool nb_scs_decode_feedback(const nb_scs_response_t *resp, nb_scs_feedback_t *out) {
    if (resp == NULL || out == NULL || resp->param_len != NB_SCS_FEEDBACK_LEN) {
        return false;
    }
    const uint8_t *p = resp->params;
    out->position = nb_scs_decode_u16(p[0], p[1]);
    out->speed = nb_scs_decode_speed(nb_scs_decode_u16(p[2], p[3]));
    out->load = nb_scs_decode_load(nb_scs_decode_u16(p[4], p[5]));
    out->voltage_dv = p[6];
    out->temperature_c = p[7];
    return true;
}

float nb_scs_counts_to_deg(int32_t counts) {
    return (float)counts * DEG_PER_COUNT;
}

uint16_t nb_scs_deg_to_counts(float degrees) {
    float counts = roundf(degrees / DEG_PER_COUNT);
    if (!(counts > 0.0f)) {  // also catches NaN
        return 0;
    }
    if (counts > (float)NB_SCS_POSITION_MAX) {
        return NB_SCS_POSITION_MAX;
    }
    return (uint16_t)counts;
}

uint32_t nb_scs_voltage_mv(uint8_t decivolts) {
    return (uint32_t)decivolts * 100u;
}

float nb_scs_load_percent(int16_t load) {
    return (float)load / 10.0f;
}
