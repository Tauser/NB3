#ifndef NB_MEMORY_H
#define NB_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nb_clock.h"
#include "nb_json.h"

#ifdef __cplusplus
extern "C" {
#endif

// Medium and long memory (docs/VIDA.md §5). Long records (profile, facts,
// preferences) and episodes live in RAM here; the card keeps them as one
// JSON snapshot in the two CRC slots of nb_store plus a log of episode
// frames appended since that snapshot. The I/O goes through nb_mem_io_t, so
// the shell plugs in the store task and the host tests plug in files in RAM
// that can lose power at any write.
//
// - Episodes fade: importance(now) = initial * 2^(-age / half-life), computed
//   on read, never decremented on the card. Episodes of unknown age keep
//   their initial importance for ranking but never enter the mood bias.
// - An episode about a long record stores only its id (`about`), never a
//   copy of its text, so correcting the record needs no rewrite elsewhere
//   and forgetting it takes the episode along.
// - Forgetting writes a snapshot carrying the forgotten ids (a pending
//   mark), empties the log, then writes a clean snapshot. Success is only
//   reported after the last step. A boot that finds the mark filters those
//   ids out of the log and finishes the job before anything else.
// - Ids are never reused: the next id is part of every snapshot.

#define NB_MEM_SCHEMA 1
#define NB_MEM_TEXT_MAX 96  // bytes, terminator included
#define NB_MEM_KEY_MAX 24
#define NB_MEM_LONG_MAX 64
#define NB_MEM_EPISODE_MAX 96
#define NB_MEM_PENDING_MAX (2 * (NB_MEM_LONG_MAX + NB_MEM_EPISODE_MAX))
// Buffers the shell allocates for the JSON of a full snapshot.
#define NB_MEM_SCRATCH_SIZE (96u * 1024u)
#define NB_MEM_TOKENS 5120

#define NB_MEM_BIAS_MAX 0.15f   // same limit as the mood core's bias
#define NB_MEM_BIAS_GAIN 0.05f  // bias per unit of importance x affect
#define NB_MEM_FADED 0.05f      // compaction drops episodes below this
#define NB_MEM_COMPACT_FRAMES 32

typedef enum {
    NB_MEM_PROFILE = 0,  // keyed: "nome", "apelido", "idioma"...
    NB_MEM_FACT,
    NB_MEM_PREFERENCE,   // optionally keyed; same key replaces
    NB_MEM_EPISODE,
    NB_MEM_KIND_COUNT,
} nb_mem_kind_t;

typedef enum {
    NB_EP_CONVERSATION = 0,
    NB_EP_AFFECTION,  // petting, kind words
    NB_EP_HURT,       // hard knock, fault, harsh words
    NB_EP_NOTE,       // something the conversation asked to remember
    NB_EP_TYPE_COUNT,
} nb_ep_type_t;

typedef struct {
    uint32_t id;  // 0 = none
    nb_mem_kind_t kind;
    nb_stamp_t when;
    char key[NB_MEM_KEY_MAX];
    char text[NB_MEM_TEXT_MAX];
    uint32_t uses;  // long records: times recalled or re-told
    // Episodes only.
    nb_ep_type_t type;
    float valence, arousal;  // -1..1
    float importance;        // initial, 0..1
    uint32_t about;          // id of the long record it is about, 0 = none
} nb_mem_record_t;

typedef enum {
    NB_MEM_OK = 0,
    NB_MEM_UPDATED,    // remember: an existing record was refreshed
    NB_MEM_NOT_FOUND,
    NB_MEM_FULL,
    NB_MEM_INVALID,    // bad kind, empty text, missing profile key
    NB_MEM_IO,         // the card failed; RAM is ahead of it
    NB_MEM_UNREADABLE, // load: a valid snapshot we cannot read; nothing is written
} nb_mem_status_t;

// Called by the io for the payload it read. `seq` is the slot's max_seq or
// the frame's seq. Return false to stop.
typedef bool (*nb_mem_visit_t)(const uint8_t *payload, uint32_t length, uint32_t seq,
                               void *arg);

typedef struct {
    void *ctx;
    // Newest valid snapshot: visit once, or *found = false when none.
    // Returns false when the card cannot be read.
    bool (*read_slot)(void *ctx, nb_mem_visit_t visit, void *arg, bool *found);
    bool (*write_slot)(void *ctx, const void *payload, uint32_t length, uint32_t max_seq);
    bool (*append)(void *ctx, uint32_t seq, const void *payload, uint32_t length);
    // Every valid frame, in order.
    bool (*read_log)(void *ctx, nb_mem_visit_t visit, void *arg);
    bool (*clear_log)(void *ctx);
} nb_mem_io_t;

typedef struct {
    nb_mem_record_t longs[NB_MEM_LONG_MAX];
    uint16_t long_count;
    nb_mem_record_t episodes[NB_MEM_EPISODE_MAX];  // oldest first
    uint16_t episode_count;
    uint32_t next_id;
    uint32_t next_seq;
    uint32_t pending[NB_MEM_PENDING_MAX];  // forgotten ids not yet gone from the card
    uint16_t pending_count;
    bool dirty;           // RAM holds changes no snapshot has
    uint16_t log_frames;  // frames since the last snapshot
    uint16_t rejected;    // records dropped at load as malformed
    bool locked;          // the snapshot on the card is unreadable: never write
    char *scratch;
    size_t scratch_cap;
    nb_json_tok_t *toks;
    int toks_cap;
} nb_memory_t;

typedef struct {
    uint16_t count[NB_MEM_KIND_COUNT];
} nb_mem_forgot_t;

// scratch and toks are at least NB_MEM_SCRATCH_SIZE and NB_MEM_TOKENS.
void nb_memory_init(nb_memory_t *m, char *scratch, size_t scratch_cap, nb_json_tok_t *toks,
                    int toks_cap);

// Snapshot, then the log frames after it. Finishes an interrupted forget
// before returning. NB_MEM_IO leaves whatever could be read;
// NB_MEM_UNREADABLE locks the memory against writes, so a snapshot from a
// newer firmware is never pushed out by this one.
nb_mem_status_t nb_memory_load(nb_memory_t *m, const nb_mem_io_t *io);

// Long record; written to the card before returning. A profile needs a
// key; a keyed record with an existing key, or a fact with the same text,
// refreshes that record (NB_MEM_UPDATED, same id).
nb_mem_status_t nb_memory_remember(nb_memory_t *m, const nb_mem_io_t *io, nb_mem_kind_t kind,
                                   const char *key, const char *text, nb_stamp_t when,
                                   uint32_t *id);

// Replaces the text of a record. Written to both slots before returning.
nb_mem_status_t nb_memory_correct(nb_memory_t *m, const nb_mem_io_t *io, uint32_t id,
                                  const char *text);

typedef struct {
    nb_ep_type_t type;
    float valence, arousal;
    float importance;  // 0 = the type's default
    uint32_t about;    // 0, or a long record id that exists
    const char *text;  // may be NULL
} nb_mem_episode_t;

// Appends the episode to the log at once. When the episodes are full, the
// one that matters least now makes room.
nb_mem_status_t nb_memory_episode(nb_memory_t *m, const nb_mem_io_t *io,
                                  const nb_mem_episode_t *ep, nb_stamp_t when,
                                  const nb_clock_t *clock, uint64_t now_mono_ms, uint32_t *id);

// Forgetting a long record takes its episodes along. Success (NB_MEM_OK)
// only once no valid copy is left on the card; out says what went.
nb_mem_status_t nb_memory_forget_id(nb_memory_t *m, const nb_mem_io_t *io, uint32_t id,
                                    nb_mem_forgot_t *out);
nb_mem_status_t nb_memory_forget_kind(nb_memory_t *m, const nb_mem_io_t *io,
                                      nb_mem_kind_t kind, nb_mem_forgot_t *out);
nb_mem_status_t nb_memory_forget_all(nb_memory_t *m, const nb_mem_io_t *io,
                                     nb_mem_forgot_t *out);

// Writes a snapshot if RAM is ahead of the card (recall counts, rebased
// stamps, failed writes).
nb_mem_status_t nb_memory_flush(nb_memory_t *m, const nb_mem_io_t *io);

bool nb_memory_wants_compaction(const nb_memory_t *m);
// Drops faded episodes, writes a snapshot with the rest, empties the log.
nb_mem_status_t nb_memory_compact(nb_memory_t *m, const nb_mem_io_t *io,
                                  const nb_clock_t *clock, uint64_t now_mono_ms);

// Gives this boot's stamps their wall time once the clock syncs or moves.
bool nb_memory_rebase(nb_memory_t *m, const nb_clock_t *clock);

// Importance of an episode now. *known is false when its age is unknown;
// the initial importance is returned then.
float nb_memory_importance(const nb_mem_record_t *ep, const nb_clock_t *clock,
                           uint64_t now_mono_ms, bool *known);

// Shift of the mood's resting point from recent episodes, each within
// ±NB_MEM_BIAS_MAX. A pure function of the episodes and the time.
void nb_memory_bias(const nb_memory_t *m, const nb_clock_t *clock, uint64_t now_mono_ms,
                    float *valence, float *arousal);

typedef struct {
    const char *words;  // NULL or "" = everything
    int kind;           // nb_mem_kind_t, or -1 for any
    int64_t max_age_s;  // 0 = any age; otherwise records of unknown age are left out
} nb_mem_query_t;

typedef struct {
    const nb_mem_record_t *record;  // valid until the next change to the memory
    float score;
} nb_mem_hit_t;

// Best matches first. Long records found count one more use (written with
// the next snapshot). Words shorter than 3 bytes are ignored; case and
// Portuguese accents do not matter.
int nb_memory_recall(nb_memory_t *m, const nb_mem_query_t *q, const nb_clock_t *clock,
                     uint64_t now_mono_ms, nb_mem_hit_t *hits, int max_hits);

const nb_mem_record_t *nb_memory_get(const nb_memory_t *m, uint32_t id);

const char *nb_memory_kind_name(nb_mem_kind_t kind);
bool nb_memory_kind_from_name(const char *name, nb_mem_kind_t *kind);
const char *nb_memory_episode_name(nb_ep_type_t type);
bool nb_memory_episode_from_name(const char *name, nb_ep_type_t *type);

#ifdef __cplusplus
}
#endif

#endif  // NB_MEMORY_H
