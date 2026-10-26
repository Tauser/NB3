#ifndef NB_CLOCK_H
#define NB_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wall-clock trust for the memory (docs/VIDA.md §5.1). There is no battery
// RTC: xiaozhi sets the system clock from the server at each boot with
// network, and the clock survives a software reset. The shell feeds samples
// of the wall clock (seconds) and the monotonic clock (ms since boot); this
// core decides how far the wall clock can be trusted and stamps records so
// their age stays right when the time arrives or is corrected later.
//
// xiaozhi adds the time-zone offset before setting the clock, so the wall
// value is local time posing as UTC: ages are right, time of day comes from
// gmtime(), and a zone change shows up as one more correction.

typedef enum {
    NB_CLOCK_INVALID = 0,  // below the floor, or below the last time seen
    NB_CLOCK_PLAUSIBLE,    // above the floor but not synced this boot
    NB_CLOCK_SYNCED,       // a sync was seen this boot: the only "valid" level
} nb_clock_level_t;

// A jump of the wall clock against the monotonic one larger than this is a
// sync (or a correction).
#define NB_CLOCK_JUMP_MS 2000

typedef struct {
    uint32_t boot_id;
    uint64_t mono_ms;  // ms since that boot
    int64_t wall_s;    // valid only when has_wall
    bool has_wall;
} nb_stamp_t;

typedef struct {
    int64_t floor_s;      // no real time is earlier than this
    int64_t last_seen_s;  // latest synced time seen, persisted; 0 = none
    uint32_t boot_id;
    nb_clock_level_t level;
    bool started;
    int64_t offset_ms;    // wall - mono at the last sample
    uint32_t syncs;       // syncs and corrections seen this boot
    bool last_seen_dirty; // last_seen_s changed and should be persisted
} nb_clock_t;

// last_seen_s and boot_id come from storage (0 and a fresh id if none).
void nb_clock_init(nb_clock_t *c, int64_t floor_s, int64_t last_seen_s, uint32_t boot_id);

// Feeds one sample. Returns true if the level or the sync count changed:
// time to rebase this boot's stamps and maybe persist last_seen.
bool nb_clock_sample(nb_clock_t *c, int64_t wall_s, uint64_t mono_ms);

nb_clock_level_t nb_clock_level(const nb_clock_t *c);

// Stamp for a record made now (at mono_ms). It carries a wall time only
// when the clock is synced.
nb_stamp_t nb_clock_stamp(const nb_clock_t *c, uint64_t mono_ms);

// Recomputes the wall time of a stamp from this boot from its monotonic
// time, with the current sync. Returns true if the stamp changed. Stamps
// from other boots, and any stamp while not synced, are left alone.
bool nb_clock_rebase(const nb_clock_t *c, nb_stamp_t *s);

// Age of a stamp in seconds, never negative. False when unknown: the
// stamp has no wall time, or the clock is not synced.
bool nb_clock_age_s(const nb_clock_t *c, const nb_stamp_t *s, uint64_t now_mono_ms,
                    int64_t *age_s);

// Wall time now, valid only when synced.
bool nb_clock_now_s(const nb_clock_t *c, uint64_t now_mono_ms, int64_t *wall_s);

const char *nb_clock_level_name(nb_clock_level_t level);

#ifdef __cplusplus
}
#endif

#endif  // NB_CLOCK_H
