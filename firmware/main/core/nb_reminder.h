#ifndef NB_REMINDER_H
#define NB_REMINDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nb_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

// Timers and reminders the robot keeps by itself (docs/ROADMAP.md stage 5):
// the AI asks for them, the robot fires them with no cloud needed.
//
// - A timer ("in 20 minutes") is due at a monotonic time of this boot, and
//   also at a wall time once the clock is synced (at once if it is, or as
//   soon as it syncs). A reminder ("at 15:30") needs the synced clock.
// - Only what has a wall time survives a reboot: a timer set before the
//   clock synced and not dated by the next boot is dropped at load, and
//   counted.
// - What came due while the robot was off fires at the first update with
//   the clock synced, telling how late it is.
// - Time only comes from the caller; storage is a versioned byte block the
//   shell keeps (NVS), decoded tolerantly.

#define NB_REMINDER_MAX 16
#define NB_REMINDER_TEXT_MAX 64  // bytes, terminator included
#define NB_REMINDER_MAX_AHEAD_S (366LL * 24 * 3600)
#define NB_REMINDER_BLOB_MAX \
    (13 + NB_REMINDER_MAX * (4 + 4 + 8 + 8 + 1 + NB_REMINDER_TEXT_MAX))

typedef struct {
    uint32_t id;  // 0 = free slot
    uint32_t boot_id;
    uint64_t due_mono_ms;  // valid in boot_id
    int64_t due_wall_s;    // valid when has_wall
    bool has_wall;
    char text[NB_REMINDER_TEXT_MAX];
} nb_reminder_entry_t;

typedef struct {
    nb_reminder_entry_t entries[NB_REMINDER_MAX];
    uint32_t next_id;
    bool dirty;  // changed since the last encode: persist it
} nb_reminders_t;

typedef enum {
    NB_REMINDER_OK = 0,
    NB_REMINDER_FULL,
    NB_REMINDER_NO_CLOCK,  // a time of day without a synced clock
    NB_REMINDER_PAST,      // the time already went by
    NB_REMINDER_TOO_FAR,
    NB_REMINDER_INVALID,   // empty text, zero delay
} nb_reminder_status_t;

typedef struct {
    uint32_t id;
    char text[NB_REMINDER_TEXT_MAX];
    int64_t late_s;  // how long after its time it fired (0 when on time)
} nb_reminder_fired_t;

void nb_reminders_init(nb_reminders_t *r);

nb_reminder_status_t nb_reminders_add_in(nb_reminders_t *r, uint32_t seconds, const char *text,
                                         const nb_clock_t *clock, uint64_t now_mono_ms,
                                         uint32_t *id);
nb_reminder_status_t nb_reminders_add_at(nb_reminders_t *r, int64_t wall_s, const char *text,
                                         const nb_clock_t *clock, uint64_t now_mono_ms,
                                         uint32_t *id);

bool nb_reminders_cancel(nb_reminders_t *r, uint32_t id);
int nb_reminders_cancel_all(nb_reminders_t *r);

// Seconds until an entry is due now; false when unknown (no wall time and
// another boot, which load already dropped).
bool nb_reminders_remaining_s(const nb_reminder_entry_t *e, const nb_clock_t *clock,
                              uint64_t now_mono_ms, int64_t *seconds);

// Dates this boot's timers once the clock syncs, then takes out every entry
// that is due, filling fired (oldest due first). Returns how many fired.
int nb_reminders_update(nb_reminders_t *r, const nb_clock_t *clock, uint64_t now_mono_ms,
                        nb_reminder_fired_t *fired, int max_fired);

int nb_reminders_count(const nb_reminders_t *r);

// Storage: encode returns the bytes written (0 if cap is too small). decode
// keeps only entries that can still fire in this boot (those with a wall
// time) and returns how many were dropped; malformed input leaves r empty.
size_t nb_reminders_encode(const nb_reminders_t *r, uint8_t *out, size_t cap);
int nb_reminders_decode(nb_reminders_t *r, const uint8_t *in, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // NB_REMINDER_H
