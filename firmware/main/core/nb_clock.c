#include "nb_clock.h"

#include <stddef.h>
#include <string.h>

void nb_clock_init(nb_clock_t *c, int64_t floor_s, int64_t last_seen_s, uint32_t boot_id) {
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->floor_s = floor_s;
    c->last_seen_s = last_seen_s;
    c->boot_id = boot_id;
    c->level = NB_CLOCK_INVALID;
}

// Level of an unsynced clock reading: plausible only above the floor and not
// behind the latest synced time seen before.
static nb_clock_level_t unsynced_level(const nb_clock_t *c, int64_t wall_s) {
    if (wall_s < c->floor_s) {
        return NB_CLOCK_INVALID;
    }
    if (c->last_seen_s > 0 && wall_s * 1000 < c->last_seen_s * 1000 - NB_CLOCK_JUMP_MS) {
        return NB_CLOCK_INVALID;
    }
    return NB_CLOCK_PLAUSIBLE;
}

bool nb_clock_sample(nb_clock_t *c, int64_t wall_s, uint64_t mono_ms) {
    if (c == NULL) {
        return false;
    }
    const int64_t offset = wall_s * 1000 - (int64_t)mono_ms;
    const nb_clock_level_t before = c->level;
    const uint32_t syncs_before = c->syncs;

    if (!c->started) {
        c->started = true;
        c->level = unsynced_level(c, wall_s);
    } else {
        int64_t jump = offset - c->offset_ms;
        if (jump < 0) {
            jump = -jump;
        }
        // The wall clock moved against the monotonic one: xiaozhi set it.
        if (jump > NB_CLOCK_JUMP_MS && wall_s >= c->floor_s) {
            c->level = NB_CLOCK_SYNCED;
            c->syncs++;
        } else if (c->level != NB_CLOCK_SYNCED) {
            c->level = unsynced_level(c, wall_s);
        }
    }
    c->offset_ms = offset;

    // A synced time is the truth, even below what was seen before (that
    // earlier time was wrong, e.g. a server in the future).
    if (c->level == NB_CLOCK_SYNCED && wall_s != c->last_seen_s &&
        (c->syncs != syncs_before || wall_s > c->last_seen_s)) {
        c->last_seen_s = wall_s;
        c->last_seen_dirty = true;
    }
    return c->level != before || c->syncs != syncs_before;
}

nb_clock_level_t nb_clock_level(const nb_clock_t *c) {
    return c != NULL ? c->level : NB_CLOCK_INVALID;
}

static int64_t wall_at(const nb_clock_t *c, uint64_t mono_ms) {
    const int64_t ms = c->offset_ms + (int64_t)mono_ms;
    return ms >= 0 ? ms / 1000 : -((-ms + 999) / 1000);
}

nb_stamp_t nb_clock_stamp(const nb_clock_t *c, uint64_t mono_ms) {
    nb_stamp_t s = {0, mono_ms, 0, false};
    if (c == NULL) {
        return s;
    }
    s.boot_id = c->boot_id;
    if (c->level == NB_CLOCK_SYNCED) {
        s.wall_s = wall_at(c, mono_ms);
        s.has_wall = true;
    }
    return s;
}

bool nb_clock_rebase(const nb_clock_t *c, nb_stamp_t *s) {
    if (c == NULL || s == NULL || c->level != NB_CLOCK_SYNCED || s->boot_id != c->boot_id) {
        return false;
    }
    const int64_t wall = wall_at(c, s->mono_ms);
    if (s->has_wall && s->wall_s == wall) {
        return false;
    }
    s->wall_s = wall;
    s->has_wall = true;
    return true;
}

bool nb_clock_now_s(const nb_clock_t *c, uint64_t now_mono_ms, int64_t *wall_s) {
    if (c == NULL || wall_s == NULL || c->level != NB_CLOCK_SYNCED) {
        return false;
    }
    *wall_s = wall_at(c, now_mono_ms);
    return true;
}

bool nb_clock_age_s(const nb_clock_t *c, const nb_stamp_t *s, uint64_t now_mono_ms,
                    int64_t *age_s) {
    int64_t now;
    if (s == NULL || age_s == NULL || !s->has_wall || !nb_clock_now_s(c, now_mono_ms, &now)) {
        return false;
    }
    *age_s = now > s->wall_s ? now - s->wall_s : 0;
    return true;
}

const char *nb_clock_level_name(nb_clock_level_t level) {
    switch (level) {
    case NB_CLOCK_INVALID: return "invalid";
    case NB_CLOCK_PLAUSIBLE: return "plausible";
    case NB_CLOCK_SYNCED: return "synced";
    default: return "unknown";
    }
}
