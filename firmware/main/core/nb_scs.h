#ifndef NB_SCS_H
#define NB_SCS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Feetech SCS-series (SCS0009) serial-servo protocol: bytes in, bytes out.
// No I/O, no clock, no allocation; every buffer is caller-provided and every
// builder checks its capacity.
//
// Instruction frame: FF FF id len instr params... checksum
// Status frame:      FF FF id len error params... checksum
//   len      = number of params + 2
//   checksum = ~(id + len + instr/error + sum(params)) & 0xFF
//
// Byte order: the SCS series is big-endian on the wire. The memory-table
// labels "_L"/"_H" are addresses, not significance: the HIGH byte of a 16-bit
// value lives at the LOWER address (the Feetech library selects this with
// End = 1 for SCSCL). STS-series servos are the opposite; do not mix them up.
//
// Half-duplex adapters (FE-TTLinker) can loop the transmitted request back
// into the receiver. The shell must drop that echo with nb_scs_echo_len()
// before parsing, because an echoed read request is a structurally valid
// frame whose "error" byte (the instruction) is non-zero.

#define NB_SCS_HEADER 0xFFu
#define NB_SCS_ID_BROADCAST 0xFEu  // never answers
#define NB_SCS_ID_ANY 0xFFu        // parser wildcard: accept any reply id

#define NB_SCS_INSTR_PING 0x01u
#define NB_SCS_INSTR_READ 0x02u
#define NB_SCS_INSTR_WRITE 0x03u
#define NB_SCS_INSTR_SYNC_WRITE 0x83u

// Error byte of a status frame (bit set = condition active).
#define NB_SCS_ERR_VOLTAGE 0x01u
#define NB_SCS_ERR_ANGLE 0x02u
#define NB_SCS_ERR_OVERHEAT 0x04u
#define NB_SCS_ERR_RANGE 0x08u
#define NB_SCS_ERR_CHECKSUM 0x10u
#define NB_SCS_ERR_OVERLOAD 0x20u
#define NB_SCS_ERR_INSTRUCTION 0x40u

// SRAM memory-table addresses used by NB3. Multi-byte values start at the
// address given here (high byte first).
#define NB_SCS_REG_TORQUE_ENABLE 0x28u  // 1 byte: 0 = free, 1 = hold
#define NB_SCS_REG_GOAL_POSITION 0x2Au  // 2 bytes, counts 0..1023
#define NB_SCS_REG_GOAL_TIME 0x2Cu      // 2 bytes, ms (0 = use speed)
#define NB_SCS_REG_GOAL_SPEED 0x2Eu     // 2 bytes
#define NB_SCS_REG_PRESENT_POSITION 0x38u  // 2 bytes, counts
#define NB_SCS_REG_PRESENT_SPEED 0x3Au     // 2 bytes, bit 15 = sign
#define NB_SCS_REG_PRESENT_LOAD 0x3Cu      // 2 bytes, see nb_scs_decode_load
#define NB_SCS_REG_PRESENT_VOLTAGE 0x3Eu   // 1 byte, 0.1 V
#define NB_SCS_REG_PRESENT_TEMPERATURE 0x3Fu  // 1 byte, deg C

// Goal position, time and speed are contiguous (0x2A..0x2F): one write sets all.
#define NB_SCS_GOAL_BLOCK_LEN 6u
// Present position .. temperature are contiguous (0x38..0x3F): one read gets all.
#define NB_SCS_FEEDBACK_LEN 8u

#define NB_SCS_POSITION_MAX 1023u
#define NB_SCS_RESPONSE_MAX_PARAMS 8u
// Largest frame any builder here can be asked for in the NB3 use (sync write
// of two goal blocks is 22 bytes); also a safe size for a receive buffer.
#define NB_SCS_FRAME_MAX 32u

typedef enum {
    NB_SCS_PARSE_OK = 0,      // frame valid; check resp->error for servo faults
    NB_SCS_PARSE_INCOMPLETE,  // a frame starts but more bytes are needed: keep reading
    NB_SCS_PARSE_NO_FRAME,    // no valid frame for the expected id in the bytes
    NB_SCS_PARSE_BAD_CHECKSUM,  // frames were found but every one failed its checksum
    NB_SCS_PARSE_BAD_ARG,     // NULL pointer
} nb_scs_parse_status_t;

typedef struct {
    uint8_t id;
    uint8_t error;  // NB_SCS_ERR_* bits; 0 = healthy
    uint8_t param_len;
    uint8_t params[NB_SCS_RESPONSE_MAX_PARAMS];
} nb_scs_response_t;

typedef struct {
    uint8_t id;
    uint16_t position;  // counts, 0..NB_SCS_POSITION_MAX
    uint16_t time_ms;   // 0 = move at `speed` instead
    uint16_t speed;     // 0 = servo's maximum
} nb_scs_goal_t;

typedef struct {
    uint16_t position;   // counts
    int16_t speed;       // signed, sign bit already decoded
    int16_t load;        // signed per-mille of drive output, -1000..1000
    uint8_t voltage_dv;  // 0.1 V units
    uint8_t temperature_c;
} nb_scs_feedback_t;

// ---- checksum and 16-bit helpers -----------------------------------------

// ~(sum of `len` bytes). Pass the bytes from `id` up to the last param.
uint8_t nb_scs_checksum(const uint8_t *bytes, size_t len);

uint16_t nb_scs_decode_u16(uint8_t high, uint8_t low);
void nb_scs_encode_u16(uint16_t value, uint8_t *high, uint8_t *low);

// ---- request builders -----------------------------------------------------
// Each returns the number of bytes written to `out`, or 0 when an argument
// is invalid (NULL, id 0xFF, too many bytes for the length field) or `cap`
// is too small. `out` is untouched on failure.

size_t nb_scs_build_ping(uint8_t *out, size_t cap, uint8_t id);
size_t nb_scs_build_read(uint8_t *out, size_t cap, uint8_t id, uint8_t addr, uint8_t count);
size_t nb_scs_build_write(uint8_t *out, size_t cap, uint8_t id, uint8_t addr,
                          const uint8_t *data, uint8_t data_len);

// Same `data_len` bytes at `addr` for `count` servos; `data` holds count
// consecutive blocks, `ids[i]` owns block i. Sent to the broadcast id, so no
// servo answers.
size_t nb_scs_build_sync_write(uint8_t *out, size_t cap, uint8_t addr, uint8_t data_len,
                               const uint8_t *ids, const uint8_t *data, uint8_t count);

size_t nb_scs_build_torque(uint8_t *out, size_t cap, uint8_t id, bool enable);
size_t nb_scs_build_goal(uint8_t *out, size_t cap, const nb_scs_goal_t *goal);
// All `count` goals in a single frame so the servos start together.
size_t nb_scs_build_sync_goal(uint8_t *out, size_t cap, const nb_scs_goal_t *goals,
                              uint8_t count);
// Reads present position, speed, load, voltage and temperature in one go.
size_t nb_scs_build_read_feedback(uint8_t *out, size_t cap, uint8_t id);

// ---- reply handling -------------------------------------------------------

// Number of leading bytes of `rx` that are the adapter echoing back `tx`
// (tx_len when rx starts with the whole request, else 0). Skip that many
// bytes before parsing. A partial echo returns 0: keep reading until at least
// tx_len bytes arrived.
size_t nb_scs_echo_len(const uint8_t *rx, size_t rx_len, const uint8_t *tx, size_t tx_len);

// Finds the first valid status frame from `expect_id` (NB_SCS_ID_ANY for any
// id) in rx[0..len). Leading garbage and frames that fail checksum, length or
// id are skipped by resyncing on the next FF FF. On OK, *consumed (optional)
// is the offset just past the frame.
nb_scs_parse_status_t nb_scs_parse_response(const uint8_t *rx, size_t len, uint8_t expect_id,
                                            nb_scs_response_t *out, size_t *consumed);

// ---- register decoding ----------------------------------------------------

// Load register: bits 9..0 = drive magnitude 0..1000 (per-mille of the
// motor supply), bit 10 = direction. Returns magnitude, negated when the
// direction bit is set (the Feetech library's convention).
int16_t nb_scs_decode_load(uint16_t raw);
// Speed register: bit 15 = direction, bits 14..0 = magnitude.
int16_t nb_scs_decode_speed(uint16_t raw);

// Decodes the NB_SCS_FEEDBACK_LEN params of a read_feedback reply.
bool nb_scs_decode_feedback(const nb_scs_response_t *resp, nb_scs_feedback_t *out);

// ---- engineering units ----------------------------------------------------

// One count is 5/16 degree (0.3125), so the 0..1023 range spans ~320 degrees.
float nb_scs_counts_to_deg(int32_t counts);
// Rounds to nearest and clamps to 0..NB_SCS_POSITION_MAX.
uint16_t nb_scs_deg_to_counts(float degrees);
// 0.1 V units to millivolts.
uint32_t nb_scs_voltage_mv(uint8_t decivolts);
// Signed per-mille load to percent of full drive.
float nb_scs_load_percent(int16_t load);

#ifdef __cplusplus
}
#endif

#endif  // NB_SCS_H
