#ifndef NB_STORE_H
#define NB_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// On-card formats of the memory (docs/VIDA.md §5.2), with no file access:
// the shell reads and writes bytes, this core decides what they mean. FATFS
// is weak against power loss and "write a temp file, then rename" is not
// atomic on it, so nothing here relies on rename:
//
// - Long records live in two slots (A and B). Each slot is a header plus a
//   payload; the CRC32 covers the whole header except itself and the
//   payload, so a damaged generation or length can never make an older
//   slot look newer. Writing goes to the slot with the lower or invalid
//   generation; loading takes the highest valid generation. The only valid
//   slot is never overwritten.
// - Episodes go to an append-only log of frames {magic, length, seq,
//   payload, CRC32}. Reading stops at the first invalid frame; the next
//   append overwrites a torn tail.
//
// Files have a fixed size: the shell creates each one once at its final
// size and afterwards only overwrites bytes inside it, so a save rewrites
// neither the FAT nor the file's place in it (a torn FAT sector can damage
// other files). So:
// - A slot file holds one slot at offset 0 followed by leftovers;
//   nb_slot_size says from the header how many bytes make the slot.
// - The log is overwritten in place: each append is the frame followed by
//   NB_STORE_LOG_END zero bytes (nb_log_entry_encode), so the scan stops
//   right after the newest frame and never reads frames left over from
//   before the last clear. Clearing zeroes the first NB_STORE_LOG_END bytes.

#define NB_STORE_SLOT_MAGIC 0x4C53424Eu   // "NBSL"
#define NB_STORE_FRAME_MAGIC 0x52464E4Eu  // "NNFR"
#define NB_STORE_FORMAT_VERSION 1u
#define NB_STORE_SLOT_HEADER_SIZE 24u
#define NB_STORE_FRAME_OVERHEAD 16u
#define NB_STORE_LOG_END 4u
// Larger payloads are treated as corruption.
#define NB_STORE_MAX_PAYLOAD (256u * 1024u)

uint32_t nb_store_crc32(uint32_t crc, const void *data, size_t len);

typedef struct {
    uint32_t generation;
    uint32_t max_seq;  // last log frame already folded into this slot
    uint32_t length;   // payload bytes
} nb_slot_info_t;

// Writes header + payload into out (capacity out_cap). Returns the bytes
// written, or 0 if it does not fit or the payload is too large.
size_t nb_slot_encode(uint32_t generation, uint32_t max_seq, const void *payload,
                      uint32_t length, uint8_t *out, size_t out_cap);

// Validates a slot read from the card (size = bytes read). On success fills
// info and points *payload at the payload inside buf.
bool nb_slot_decode(const uint8_t *buf, size_t size, nb_slot_info_t *info,
                    const uint8_t **payload);

typedef struct {
    int load;           // 0 = A, 1 = B, -1 = neither is valid
    int write;          // slot to overwrite next: never the only valid one
    uint32_t next_gen;  // generation for that write
} nb_slot_choice_t;

// Bytes the slot starting at header occupies (header + payload), or 0 if
// the header is not a slot of this format. size = bytes available.
size_t nb_slot_size(const uint8_t *header, size_t size);

nb_slot_choice_t nb_slot_choose(bool a_valid, const nb_slot_info_t *a, bool b_valid,
                                const nb_slot_info_t *b);

// Frame size for a payload of length bytes.
size_t nb_frame_size(uint32_t length);
size_t nb_frame_encode(uint32_t seq, const void *payload, uint32_t length, uint8_t *out,
                       size_t out_cap);

// Called for each valid frame, in order. Return false to stop early.
// A log append: the frame and its end marker. Returns the bytes, or 0.
size_t nb_log_entry_size(uint32_t length);
size_t nb_log_entry_encode(uint32_t seq, const void *payload, uint32_t length, uint8_t *out,
                           size_t out_cap);

typedef bool (*nb_frame_visit_t)(uint32_t seq, const uint8_t *payload, uint32_t length,
                                 void *ctx);

typedef struct {
    size_t valid_bytes;  // the log is good up to here; anything after is torn
    uint32_t frames;
    uint32_t last_seq;   // 0 if no frame
} nb_log_scan_t;

nb_log_scan_t nb_log_scan(const uint8_t *buf, size_t size, nb_frame_visit_t visit, void *ctx);

#ifdef __cplusplus
}
#endif

#endif  // NB_STORE_H
