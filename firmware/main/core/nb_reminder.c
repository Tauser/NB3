#include "nb_reminder.h"

#include <string.h>

#define BLOB_MAGIC 0x4E42524Du  // "NBRM"
#define BLOB_VERSION 1u

void nb_reminders_init(nb_reminders_t *r) {
    memset(r, 0, sizeof(*r));
    r->next_id = 1;
}

static nb_reminder_entry_t *free_slot(nb_reminders_t *r) {
    for (int i = 0; i < NB_REMINDER_MAX; i++) {
        if (r->entries[i].id == 0) {
            return &r->entries[i];
        }
    }
    return NULL;
}

static bool text_ok(const char *text) {
    if (text == NULL) {
        return false;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            return true;
        }
    }
    return false;
}

// Copies at most cap-1 bytes without cutting a UTF-8 character in two.
static void copy_text(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static nb_reminder_status_t add(nb_reminders_t *r, const char *text, const nb_clock_t *clock,
                                uint64_t due_mono_ms, bool has_wall, int64_t due_wall_s,
                                uint32_t *id) {
    if (!text_ok(text)) {
        return NB_REMINDER_INVALID;
    }
    nb_reminder_entry_t *e = free_slot(r);
    if (e == NULL) {
        return NB_REMINDER_FULL;
    }
    memset(e, 0, sizeof(*e));
    e->id = r->next_id++;
    e->boot_id = clock->boot_id;
    e->due_mono_ms = due_mono_ms;
    e->has_wall = has_wall;
    e->due_wall_s = due_wall_s;
    copy_text(e->text, sizeof(e->text), text);
    r->dirty = true;
    if (id != NULL) {
        *id = e->id;
    }
    return NB_REMINDER_OK;
}

nb_reminder_status_t nb_reminders_add_in(nb_reminders_t *r, uint32_t seconds, const char *text,
                                         const nb_clock_t *clock, uint64_t now_mono_ms,
                                         uint32_t *id) {
    if (seconds == 0) {
        return NB_REMINDER_INVALID;
    }
    if ((int64_t)seconds > NB_REMINDER_MAX_AHEAD_S) {
        return NB_REMINDER_TOO_FAR;
    }
    int64_t now_s = 0;
    const bool synced = nb_clock_now_s(clock, now_mono_ms, &now_s);
    return add(r, text, clock, now_mono_ms + (uint64_t)seconds * 1000u, synced,
               now_s + (int64_t)seconds, id);
}

nb_reminder_status_t nb_reminders_add_at(nb_reminders_t *r, int64_t wall_s, const char *text,
                                         const nb_clock_t *clock, uint64_t now_mono_ms,
                                         uint32_t *id) {
    int64_t now_s = 0;
    if (!nb_clock_now_s(clock, now_mono_ms, &now_s)) {
        return NB_REMINDER_NO_CLOCK;
    }
    if (wall_s <= now_s) {
        return NB_REMINDER_PAST;
    }
    if (wall_s - now_s > NB_REMINDER_MAX_AHEAD_S) {
        return NB_REMINDER_TOO_FAR;
    }
    return add(r, text, clock, now_mono_ms + (uint64_t)(wall_s - now_s) * 1000u, true, wall_s,
               id);
}

bool nb_reminders_cancel(nb_reminders_t *r, uint32_t id) {
    for (int i = 0; id != 0 && i < NB_REMINDER_MAX; i++) {
        if (r->entries[i].id == id) {
            memset(&r->entries[i], 0, sizeof(r->entries[i]));
            r->dirty = true;
            return true;
        }
    }
    return false;
}

int nb_reminders_cancel_all(nb_reminders_t *r) {
    const int n = nb_reminders_count(r);
    for (int i = 0; i < NB_REMINDER_MAX; i++) {
        memset(&r->entries[i], 0, sizeof(r->entries[i]));
    }
    if (n > 0) {
        r->dirty = true;
    }
    return n;
}

int nb_reminders_count(const nb_reminders_t *r) {
    int n = 0;
    for (int i = 0; i < NB_REMINDER_MAX; i++) {
        n += r->entries[i].id != 0;
    }
    return n;
}

bool nb_reminders_remaining_s(const nb_reminder_entry_t *e, const nb_clock_t *clock,
                              uint64_t now_mono_ms, int64_t *seconds) {
    // Within its boot the monotonic time is exact; across boots only the wall
    // time is known.
    if (e->boot_id == clock->boot_id) {
        *seconds = ((int64_t)e->due_mono_ms - (int64_t)now_mono_ms) / 1000;
        return true;
    }
    int64_t now_s = 0;
    if (e->has_wall && nb_clock_now_s(clock, now_mono_ms, &now_s)) {
        *seconds = e->due_wall_s - now_s;
        return true;
    }
    return false;
}

int nb_reminders_update(nb_reminders_t *r, const nb_clock_t *clock, uint64_t now_mono_ms,
                        nb_reminder_fired_t *fired, int max_fired) {
    int64_t now_s = 0;
    const bool synced = nb_clock_now_s(clock, now_mono_ms, &now_s);
    // This boot's timers get a wall time as soon as there is one.
    for (int i = 0; synced && i < NB_REMINDER_MAX; i++) {
        nb_reminder_entry_t *e = &r->entries[i];
        if (e->id != 0 && !e->has_wall && e->boot_id == clock->boot_id) {
            e->due_wall_s = now_s + ((int64_t)e->due_mono_ms - (int64_t)now_mono_ms) / 1000;
            e->has_wall = true;
            r->dirty = true;
        }
    }
    int n = 0;
    while (n < max_fired) {
        int pick = -1;
        int64_t pick_late = 0;
        for (int i = 0; i < NB_REMINDER_MAX; i++) {
            const nb_reminder_entry_t *e = &r->entries[i];
            int64_t remaining;
            if (e->id == 0 || !nb_reminders_remaining_s(e, clock, now_mono_ms, &remaining) ||
                remaining > 0) {
                continue;
            }
            // Within this boot the monotonic time is exact to the ms.
            if (remaining == 0 && e->boot_id == clock->boot_id && e->due_mono_ms > now_mono_ms) {
                continue;
            }
            if (pick < 0 || -remaining > pick_late) {
                pick = i;
                pick_late = -remaining;
            }
        }
        if (pick < 0) {
            break;
        }
        fired[n].id = r->entries[pick].id;
        memcpy(fired[n].text, r->entries[pick].text, sizeof(fired[n].text));
        fired[n].late_s = pick_late;
        n++;
        memset(&r->entries[pick], 0, sizeof(r->entries[pick]));
        r->dirty = true;
    }
    return n;
}

// --- Storage: little-endian, fixed layout ---------------------------------

static void put32(uint8_t **p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        *(*p)++ = (uint8_t)(v >> (8 * i));
    }
}

static void put64(uint8_t **p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        *(*p)++ = (uint8_t)(v >> (8 * i));
    }
}

static uint32_t get32(const uint8_t **p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)*(*p)++ << (8 * i);
    }
    return v;
}

static uint64_t get64(const uint8_t **p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t)*(*p)++ << (8 * i);
    }
    return v;
}

#define ENTRY_BYTES (4 + 4 + 8 + 8 + 1 + NB_REMINDER_TEXT_MAX)

size_t nb_reminders_encode(const nb_reminders_t *r, uint8_t *out, size_t cap) {
    const int count = nb_reminders_count(r);
    const size_t need = 8 + 4 + 1 + (size_t)count * ENTRY_BYTES;
    if (out == NULL || cap < need) {
        return 0;
    }
    uint8_t *p = out;
    put32(&p, BLOB_MAGIC);
    put32(&p, BLOB_VERSION);
    put32(&p, r->next_id);
    *p++ = (uint8_t)count;
    for (int i = 0; i < NB_REMINDER_MAX; i++) {
        const nb_reminder_entry_t *e = &r->entries[i];
        if (e->id == 0) {
            continue;
        }
        put32(&p, e->id);
        put32(&p, e->boot_id);
        put64(&p, e->due_mono_ms);
        put64(&p, (uint64_t)e->due_wall_s);
        *p++ = e->has_wall ? 1u : 0u;
        memcpy(p, e->text, NB_REMINDER_TEXT_MAX);
        p += NB_REMINDER_TEXT_MAX;
    }
    return (size_t)(p - out);
}

int nb_reminders_decode(nb_reminders_t *r, const uint8_t *in, size_t len) {
    nb_reminders_init(r);
    if (in == NULL || len < 13) {
        return 0;
    }
    const uint8_t *p = in;
    if (get32(&p) != BLOB_MAGIC || get32(&p) != BLOB_VERSION) {
        return 0;
    }
    const uint32_t next_id = get32(&p);
    const int count = *p++;
    if (count > NB_REMINDER_MAX || len < 13 + (size_t)count * ENTRY_BYTES) {
        return 0;
    }
    int dropped = 0;
    uint32_t max_id = 0;
    for (int i = 0; i < count; i++) {
        nb_reminder_entry_t e;
        memset(&e, 0, sizeof(e));
        e.id = get32(&p);
        e.boot_id = get32(&p);
        e.due_mono_ms = get64(&p);
        e.due_wall_s = (int64_t)get64(&p);
        e.has_wall = *p++ != 0;
        memcpy(e.text, p, NB_REMINDER_TEXT_MAX);
        p += NB_REMINDER_TEXT_MAX;
        e.text[NB_REMINDER_TEXT_MAX - 1] = '\0';
        if (e.id > max_id) {
            max_id = e.id;
        }
        // Without a wall time it belongs to a boot that is over.
        if (e.id == 0 || !e.has_wall || !text_ok(e.text)) {
            dropped++;
            continue;
        }
        r->entries[i] = e;
    }
    r->next_id = next_id > max_id ? next_id : max_id + 1;
    r->dirty = dropped > 0;
    return dropped;
}
