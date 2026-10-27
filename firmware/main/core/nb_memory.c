#include "nb_memory.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Per episode type: half-life and default initial importance. Hurt fades
// slowest, matching the mood's slower recovery from negative feelings.
static const struct {
    const char *name;
    int64_t half_life_s;
    float importance;
} kEpisodeTypes[NB_EP_TYPE_COUNT] = {
    [NB_EP_CONVERSATION] = {"conversation", 12 * 3600, 0.4f},
    [NB_EP_AFFECTION] = {"affection", 6 * 3600, 0.5f},
    [NB_EP_HURT] = {"hurt", 48 * 3600, 0.7f},
    [NB_EP_NOTE] = {"note", 7 * 24 * 3600, 0.6f},
};

static const char *const kKindNames[NB_MEM_KIND_COUNT] = {
    [NB_MEM_PROFILE] = "profile",
    [NB_MEM_FACT] = "fact",
    [NB_MEM_PREFERENCE] = "preference",
    [NB_MEM_EPISODE] = "episode",
};

const char *nb_memory_kind_name(nb_mem_kind_t kind) {
    return (unsigned)kind < NB_MEM_KIND_COUNT ? kKindNames[kind] : "unknown";
}

bool nb_memory_kind_from_name(const char *name, nb_mem_kind_t *kind) {
    for (int i = 0; i < NB_MEM_KIND_COUNT && name != NULL; i++) {
        if (strcmp(name, kKindNames[i]) == 0) {
            *kind = (nb_mem_kind_t)i;
            return true;
        }
    }
    return false;
}

const char *nb_memory_episode_name(nb_ep_type_t type) {
    return (unsigned)type < NB_EP_TYPE_COUNT ? kEpisodeTypes[type].name : "unknown";
}

bool nb_memory_episode_from_name(const char *name, nb_ep_type_t *type) {
    for (int i = 0; i < NB_EP_TYPE_COUNT && name != NULL; i++) {
        if (strcmp(name, kEpisodeTypes[i].name) == 0) {
            *type = (nb_ep_type_t)i;
            return true;
        }
    }
    return false;
}

static float clampf(float v, float lo, float hi) {
    if (!(v >= lo)) {  // NaN too
        return lo;
    }
    return v > hi ? hi : v;
}

// --- Text -----------------------------------------------------------------

static size_t utf8_len(const unsigned char *s) {
    size_t len;
    if (s[0] < 0x80) {
        return 1;
    } else if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        len = 2;
    } else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        len = 3;
    } else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        len = 4;
    } else {
        return 0;
    }
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return len;
}

// Copies text into dst on a character boundary: control characters become
// spaces, runs of spaces collapse, the ends are trimmed and a malformed
// sequence ends the text. Returns the length.
static size_t copy_text(char *dst, size_t cap, const char *src) {
    size_t out = 0;
    bool space = false;
    const unsigned char *p = (const unsigned char *)(src != NULL ? src : "");
    while (*p != '\0') {
        const size_t len = utf8_len(p);
        if (len == 0) {
            break;
        }
        if (len == 1 && (*p <= 0x20 || *p == 0x7F)) {
            space = out > 0;
            p++;
            continue;
        }
        const size_t need = len + (space ? 1 : 0);
        if (out + need > cap - 1) {
            break;
        }
        if (space) {
            dst[out++] = ' ';
            space = false;
        }
        memcpy(dst + out, p, len);
        out += len;
        p += len;
    }
    dst[out] = '\0';
    return out;
}

// Lower-case ASCII with Portuguese accents folded (UTF-8 Latin-1 range).
static size_t fold(const char *src, char *dst, size_t cap) {
    static const char kFold[65] = "aaaaaaaceeeeiiiidnooooo/ouuuuyts" "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p != '\0' && out + 1 < cap) {
        if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            dst[out++] = kFold[p[1] - 0x80];
            p += 2;
        } else {
            const unsigned char c = *p++;
            dst[out++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }
    dst[out] = '\0';
    return out;
}

// --- Records ----------------------------------------------------------------

static nb_mem_record_t *find_long(nb_memory_t *m, uint32_t id) {
    for (uint16_t i = 0; i < m->long_count; i++) {
        if (m->longs[i].id == id) {
            return &m->longs[i];
        }
    }
    return NULL;
}

static nb_mem_record_t *find_episode(nb_memory_t *m, uint32_t id) {
    for (uint16_t i = 0; i < m->episode_count; i++) {
        if (m->episodes[i].id == id) {
            return &m->episodes[i];
        }
    }
    return NULL;
}

const nb_mem_record_t *nb_memory_get(const nb_memory_t *m, uint32_t id) {
    nb_memory_t *mm = (nb_memory_t *)m;
    const nb_mem_record_t *r = find_long(mm, id);
    return r != NULL ? r : find_episode(mm, id);
}

static bool is_pending(const nb_memory_t *m, uint32_t id) {
    for (uint16_t i = 0; i < m->pending_count; i++) {
        if (m->pending[i] == id) {
            return true;
        }
    }
    return false;
}

static void remove_at(nb_mem_record_t *arr, uint16_t *count, uint16_t i) {
    memmove(&arr[i], &arr[i + 1], (size_t)(*count - i - 1) * sizeof(arr[0]));
    (*count)--;
}

static void insert_episode(nb_memory_t *m, const nb_mem_record_t *r) {
    // Kept in id order, which is the order they happened.
    uint16_t i = m->episode_count;
    while (i > 0 && m->episodes[i - 1].id > r->id) {
        i--;
    }
    memmove(&m->episodes[i + 1], &m->episodes[i],
            (size_t)(m->episode_count - i) * sizeof(m->episodes[0]));
    m->episodes[i] = *r;
    m->episode_count++;
}

// --- Patterns ---------------------------------------------------------------

static bool counts_in_patterns(const nb_mem_record_t *e) {
    return e->type != NB_EP_NOTE && e->about == 0;
}

static void count_episode(nb_mem_patterns_t *p, const nb_mem_record_t *e) {
    if (!counts_in_patterns(e)) {
        return;
    }
    if (e->when.has_wall) {
        // Local time posing as UTC (nb_clock.h): the hour is the local one.
        const int64_t day_s = ((e->when.wall_s % 86400) + 86400) % 86400;
        p->hours[e->type][day_s / 3600]++;
        if (p->since_s == 0 || e->when.wall_s < p->since_s) {
            p->since_s = e->when.wall_s;
        }
    } else {
        p->undated[e->type]++;
    }
    p->valence_sum[e->type] += e->valence;
}

// An episode is leaving the memory: what it says about habits stays.
static void consolidate(nb_memory_t *m, const nb_mem_record_t *e) {
    count_episode(&m->patterns, e);
    m->dirty = true;
}

uint32_t nb_memory_patterns_total(const nb_mem_patterns_t *p, nb_ep_type_t type) {
    if ((unsigned)type >= NB_EP_TYPE_COUNT) {
        return 0;
    }
    uint32_t n = p->undated[type];
    for (int h = 0; h < NB_MEM_HOURS; h++) {
        n += p->hours[type][h];
    }
    return n;
}

static uint32_t patterns_all(const nb_mem_patterns_t *p) {
    uint32_t n = 0;
    for (int t = 0; t < NB_EP_TYPE_COUNT; t++) {
        n += nb_memory_patterns_total(p, (nb_ep_type_t)t);
    }
    return n;
}

void nb_memory_patterns(const nb_memory_t *m, nb_mem_patterns_t *out) {
    *out = m->patterns;
    for (uint16_t i = 0; i < m->episode_count; i++) {
        count_episode(out, &m->episodes[i]);
    }
}

// --- JSON -------------------------------------------------------------------

static void write_record(nb_json_writer_t *w, const nb_mem_record_t *r) {
    nb_jw_object_begin(w);
    nb_jw_key(w, "id");
    nb_jw_int(w, r->id);
    nb_jw_key(w, "kind");
    nb_jw_string(w, nb_memory_kind_name(r->kind));
    if (r->key[0] != '\0') {
        nb_jw_key(w, "key");
        nb_jw_string(w, r->key);
    }
    nb_jw_key(w, "text");
    nb_jw_string(w, r->text);
    nb_jw_key(w, "boot");
    nb_jw_int(w, r->when.boot_id);
    nb_jw_key(w, "mono_ms");
    nb_jw_int(w, (int64_t)r->when.mono_ms);
    if (r->when.has_wall) {
        nb_jw_key(w, "wall_s");
        nb_jw_int(w, r->when.wall_s);
    }
    if (r->kind == NB_MEM_EPISODE) {
        nb_jw_key(w, "type");
        nb_jw_string(w, nb_memory_episode_name(r->type));
        nb_jw_key(w, "valence");
        nb_jw_float(w, r->valence);
        nb_jw_key(w, "arousal");
        nb_jw_float(w, r->arousal);
        nb_jw_key(w, "importance");
        nb_jw_float(w, r->importance);
        if (r->about != 0) {
            nb_jw_key(w, "about");
            nb_jw_int(w, r->about);
        }
    } else {
        nb_jw_key(w, "uses");
        nb_jw_int(w, r->uses);
    }
    nb_jw_object_end(w);
}

static bool read_float(const char *t, const nb_json_tok_t *toks, int obj, const char *key,
                       float lo, float hi, float *out) {
    double v;
    const int i = nb_json_find(t, toks, obj, key);
    if (i < 0 || !nb_json_number(t, &toks[i], &v)) {
        return false;
    }
    *out = clampf((float)v, lo, hi);
    return true;
}

static bool read_int(const char *t, const nb_json_tok_t *toks, int obj, const char *key,
                     int64_t lo, int64_t hi, int64_t *out) {
    const int i = nb_json_find(t, toks, obj, key);
    return i >= 0 && nb_json_int(t, &toks[i], lo, hi, out);
}

static bool read_record(const char *t, const nb_json_tok_t *toks, int obj, nb_mem_record_t *r) {
    char name[24];
    int64_t v;
    memset(r, 0, sizeof(*r));
    if (toks[obj].type != NB_JSON_OBJECT) {
        return false;
    }
    if (!read_int(t, toks, obj, "id", 1, UINT32_MAX, &v)) {
        return false;
    }
    r->id = (uint32_t)v;
    int i = nb_json_find(t, toks, obj, "kind");
    if (i < 0 || !nb_json_string(t, &toks[i], name, sizeof(name)) ||
        !nb_memory_kind_from_name(name, &r->kind)) {
        return false;
    }
    char text[NB_MEM_TEXT_MAX];
    i = nb_json_find(t, toks, obj, "text");
    if (i < 0 || !nb_json_string(t, &toks[i], text, sizeof(text))) {
        return false;
    }
    copy_text(r->text, sizeof(r->text), text);
    i = nb_json_find(t, toks, obj, "key");
    if (i >= 0) {
        char key[NB_MEM_KEY_MAX];
        if (!nb_json_string(t, &toks[i], key, sizeof(key))) {
            return false;
        }
        copy_text(r->key, sizeof(r->key), key);
    }
    if (!read_int(t, toks, obj, "boot", 0, UINT32_MAX, &v)) {
        return false;
    }
    r->when.boot_id = (uint32_t)v;
    if (!read_int(t, toks, obj, "mono_ms", 0, INT64_MAX, &v)) {
        return false;
    }
    r->when.mono_ms = (uint64_t)v;
    if (read_int(t, toks, obj, "wall_s", INT64_MIN, INT64_MAX, &v)) {
        r->when.wall_s = v;
        r->when.has_wall = true;
    }
    if (r->kind == NB_MEM_EPISODE) {
        i = nb_json_find(t, toks, obj, "type");
        if (i < 0 || !nb_json_string(t, &toks[i], name, sizeof(name)) ||
            !nb_memory_episode_from_name(name, &r->type)) {
            return false;
        }
        if (!read_float(t, toks, obj, "valence", -1.0f, 1.0f, &r->valence) ||
            !read_float(t, toks, obj, "arousal", -1.0f, 1.0f, &r->arousal) ||
            !read_float(t, toks, obj, "importance", 0.0f, 1.0f, &r->importance)) {
            return false;
        }
        if (read_int(t, toks, obj, "about", 1, UINT32_MAX, &v)) {
            r->about = (uint32_t)v;
        }
    } else {
        if (r->text[0] == '\0' || (r->kind == NB_MEM_PROFILE && r->key[0] == '\0')) {
            return false;
        }
        if (read_int(t, toks, obj, "uses", 0, UINT32_MAX, &v)) {
            r->uses = (uint32_t)v;
        }
    }
    return true;
}

// "patterns": {"since": s, "<type>": {"hours": [24], "undated": n,
// "valence": sum}, ...}, only the types that counted something.
static void write_patterns(nb_json_writer_t *w, const nb_mem_patterns_t *p) {
    nb_jw_key(w, "patterns");
    nb_jw_object_begin(w);
    if (p->since_s != 0) {
        nb_jw_key(w, "since");
        nb_jw_int(w, p->since_s);
    }
    for (int t = 0; t < NB_EP_TYPE_COUNT; t++) {
        if (nb_memory_patterns_total(p, (nb_ep_type_t)t) == 0) {
            continue;
        }
        nb_jw_key(w, kEpisodeTypes[t].name);
        nb_jw_object_begin(w);
        nb_jw_key(w, "hours");
        nb_jw_array_begin(w);
        for (int h = 0; h < NB_MEM_HOURS; h++) {
            nb_jw_int(w, p->hours[t][h]);
        }
        nb_jw_array_end(w);
        nb_jw_key(w, "undated");
        nb_jw_int(w, p->undated[t]);
        nb_jw_key(w, "valence");
        nb_jw_float(w, p->valence_sum[t]);
        nb_jw_object_end(w);
    }
    nb_jw_object_end(w);
}

// A malformed type is left out whole rather than read in part.
static void read_patterns(const char *t, const nb_json_tok_t *toks, int obj,
                          nb_mem_patterns_t *p, uint16_t *rejected) {
    memset(p, 0, sizeof(*p));
    int64_t v;
    if (read_int(t, toks, obj, "since", 1, INT64_MAX, &v)) {
        p->since_s = v;
    }
    for (int type = 0; type < NB_EP_TYPE_COUNT; type++) {
        const int o = nb_json_find(t, toks, obj, kEpisodeTypes[type].name);
        if (o < 0) {
            continue;
        }
        const int hours = toks[o].type == NB_JSON_OBJECT ? nb_json_find(t, toks, o, "hours") : -1;
        nb_mem_patterns_t one;
        memset(&one, 0, sizeof(one));
        bool ok = hours >= 0 && toks[hours].type == NB_JSON_ARRAY &&
                  toks[hours].size == NB_MEM_HOURS;
        int k = hours + 1;
        for (int h = 0; ok && h < NB_MEM_HOURS; h++) {
            ok = nb_json_int(t, &toks[k], 0, UINT32_MAX, &v);
            one.hours[type][h] = (uint32_t)v;
            k = (int)toks[k].next;
        }
        ok = ok && read_int(t, toks, o, "undated", 0, UINT32_MAX, &v);
        one.undated[type] = ok ? (uint32_t)v : 0;
        ok = ok && read_float(t, toks, o, "valence", -1e9f, 1e9f, &one.valence_sum[type]);
        if (!ok) {
            (*rejected)++;
            continue;
        }
        memcpy(p->hours[type], one.hours[type], sizeof(p->hours[type]));
        p->undated[type] = one.undated[type];
        p->valence_sum[type] = one.valence_sum[type];
    }
}

static size_t encode_snapshot(nb_memory_t *m, bool with_pending) {
    nb_json_writer_t w;
    nb_jw_init(&w, m->scratch, m->scratch_cap);
    nb_jw_object_begin(&w);
    nb_jw_key(&w, "schema");
    nb_jw_int(&w, NB_MEM_SCHEMA);
    nb_jw_key(&w, "next_id");
    nb_jw_int(&w, m->next_id);
    if (with_pending && m->pending_count > 0) {
        nb_jw_key(&w, "forgetting");
        nb_jw_array_begin(&w);
        for (uint16_t i = 0; i < m->pending_count; i++) {
            nb_jw_int(&w, m->pending[i]);
        }
        nb_jw_array_end(&w);
    }
    if (patterns_all(&m->patterns) > 0) {
        write_patterns(&w, &m->patterns);
    }
    nb_jw_key(&w, "records");
    nb_jw_array_begin(&w);
    for (uint16_t i = 0; i < m->long_count; i++) {
        write_record(&w, &m->longs[i]);
    }
    for (uint16_t i = 0; i < m->episode_count; i++) {
        write_record(&w, &m->episodes[i]);
    }
    nb_jw_array_end(&w);
    nb_jw_object_end(&w);
    return nb_jw_finish(&w);
}

static bool add_loaded(nb_memory_t *m, const nb_mem_record_t *r) {
    if (r->id >= m->next_id) {
        m->next_id = r->id + 1;
    }
    if (is_pending(m, r->id) || (r->about != 0 && is_pending(m, r->about)) ||
        nb_memory_get(m, r->id) != NULL) {
        return true;  // forgotten, or seen already
    }
    if (r->kind == NB_MEM_EPISODE) {
        if (m->episode_count >= NB_MEM_EPISODE_MAX) {
            consolidate(m, &m->episodes[0]);
            remove_at(m->episodes, &m->episode_count, 0);  // the oldest
        }
        insert_episode(m, r);
    } else {
        if (m->long_count >= NB_MEM_LONG_MAX) {
            return false;
        }
        m->longs[m->long_count++] = *r;
    }
    return true;
}

typedef struct {
    nb_memory_t *m;
    uint32_t max_seq;
    bool found;
    bool parsed;
} load_ctx_t;

static bool visit_slot(const uint8_t *payload, uint32_t length, uint32_t seq, void *arg) {
    load_ctx_t *ctx = arg;
    nb_memory_t *m = ctx->m;
    const char *t = (const char *)payload;
    ctx->found = true;
    ctx->max_seq = seq;
    const int n = nb_json_parse(t, length, m->toks, m->toks_cap);
    if (n < 1 || m->toks[0].type != NB_JSON_OBJECT) {
        return false;
    }
    int64_t v;
    if (!read_int(t, m->toks, 0, "schema", 1, NB_MEM_SCHEMA, &v) ||
        !read_int(t, m->toks, 0, "next_id", 1, UINT32_MAX, &v)) {
        return false;
    }
    m->next_id = (uint32_t)v;
    int i = nb_json_find(t, m->toks, 0, "forgetting");
    if (i >= 0 && m->toks[i].type == NB_JSON_ARRAY) {
        int k = i + 1;
        for (uint32_t j = 0; j < m->toks[i].size && m->pending_count < NB_MEM_PENDING_MAX; j++) {
            if (nb_json_int(t, &m->toks[k], 1, UINT32_MAX, &v)) {
                m->pending[m->pending_count++] = (uint32_t)v;
            }
            k = (int)m->toks[k].next;
        }
    }
    // Before the records: an episode pushed out while loading adds to these.
    i = nb_json_find(t, m->toks, 0, "patterns");
    if (i >= 0 && m->toks[i].type == NB_JSON_OBJECT) {
        read_patterns(t, m->toks, i, &m->patterns, &m->rejected);
    }
    i = nb_json_find(t, m->toks, 0, "records");
    if (i < 0 || m->toks[i].type != NB_JSON_ARRAY) {
        return false;
    }
    int k = i + 1;
    for (uint32_t j = 0; j < m->toks[i].size; j++) {
        nb_mem_record_t r;
        if (!read_record(t, m->toks, k, &r) || !add_loaded(m, &r)) {
            m->rejected++;
        }
        k = (int)m->toks[k].next;
    }
    ctx->parsed = true;
    return true;
}

static bool visit_frame(const uint8_t *payload, uint32_t length, uint32_t seq, void *arg) {
    load_ctx_t *ctx = arg;
    nb_memory_t *m = ctx->m;
    if (seq >= m->next_seq) {
        m->next_seq = seq + 1;
    }
    if (seq <= ctx->max_seq) {
        return true;  // already folded into the snapshot
    }
    m->log_frames++;
    const char *t = (const char *)payload;
    const int n = nb_json_parse(t, length, m->toks, m->toks_cap);
    int64_t v;
    nb_mem_record_t r;
    const int ep = n > 0 ? nb_json_find(t, m->toks, 0, "episode") : -1;
    if (ep < 0 || !read_int(t, m->toks, 0, "schema", 1, NB_MEM_SCHEMA, &v) ||
        !read_record(t, m->toks, ep, &r) || r.kind != NB_MEM_EPISODE || !add_loaded(m, &r)) {
        m->rejected++;
    }
    return true;
}

// --- Writing ----------------------------------------------------------------

static nb_mem_status_t write_snapshot(nb_memory_t *m, const nb_mem_io_t *io, bool with_pending) {
    if (m->locked) {
        m->dirty = true;
        return NB_MEM_IO;
    }
    const size_t n = encode_snapshot(m, with_pending);
    if (n == 0) {
        return NB_MEM_FULL;  // cannot happen within the record limits
    }
    if (!io->write_slot(io->ctx, m->scratch, (uint32_t)n, m->next_seq - 1)) {
        m->dirty = true;
        return NB_MEM_IO;
    }
    m->dirty = false;
    return NB_MEM_OK;
}

// The forget protocol, also used to finish one found at boot: snapshot with
// the mark, empty log, clean snapshot. The mark leaves RAM only after the
// clean snapshot is on the card.
static nb_mem_status_t finish_forget(nb_memory_t *m, const nb_mem_io_t *io) {
    nb_mem_status_t s = write_snapshot(m, io, true);
    if (s != NB_MEM_OK) {
        return s;
    }
    if (m->locked || !io->clear_log(io->ctx)) {
        m->dirty = true;
        return NB_MEM_IO;
    }
    m->log_frames = 0;
    s = write_snapshot(m, io, false);
    if (s == NB_MEM_OK) {
        m->pending_count = 0;
    }
    return s;
}

void nb_memory_init(nb_memory_t *m, char *scratch, size_t scratch_cap, nb_json_tok_t *toks,
                    int toks_cap) {
    memset(m, 0, offsetof(nb_memory_t, scratch));
    m->next_id = 1;
    m->next_seq = 1;
    m->scratch = scratch;
    m->scratch_cap = scratch_cap;
    m->toks = toks;
    m->toks_cap = toks_cap;
}

nb_mem_status_t nb_memory_load(nb_memory_t *m, const nb_mem_io_t *io) {
    load_ctx_t ctx = {m, 0, false, false};
    bool found = false;
    if (!io->read_slot(io->ctx, visit_slot, &ctx, &found)) {
        return NB_MEM_IO;
    }
    if (found && !ctx.parsed) {
        // Intact on the card but not ours to read (a newer schema, or a bug):
        // writing would push it out of the slots, so nothing is written.
        m->locked = true;
        return NB_MEM_UNREADABLE;
    }
    if (ctx.max_seq >= m->next_seq) {
        m->next_seq = ctx.max_seq + 1;
    }
    if (!io->read_log(io->ctx, visit_frame, &ctx)) {
        return NB_MEM_IO;
    }
    if (m->pending_count > 0) {
        return finish_forget(m, io);
    }
    return NB_MEM_OK;
}

static bool same_text(const char *a, const char *b) {
    char fa[NB_MEM_TEXT_MAX], fb[NB_MEM_TEXT_MAX];
    fold(a, fa, sizeof(fa));
    fold(b, fb, sizeof(fb));
    return strcmp(fa, fb) == 0;
}

nb_mem_status_t nb_memory_remember(nb_memory_t *m, const nb_mem_io_t *io, nb_mem_kind_t kind,
                                   const char *key, const char *text, nb_stamp_t when,
                                   uint32_t *id) {
    nb_mem_record_t r;
    memset(&r, 0, sizeof(r));
    if (kind != NB_MEM_PROFILE && kind != NB_MEM_FACT && kind != NB_MEM_PREFERENCE) {
        return NB_MEM_INVALID;
    }
    copy_text(r.key, sizeof(r.key), kind == NB_MEM_FACT ? "" : key);
    if (copy_text(r.text, sizeof(r.text), text) == 0 ||
        (kind == NB_MEM_PROFILE && r.key[0] == '\0')) {
        return NB_MEM_INVALID;
    }

    nb_mem_record_t *same = NULL;
    for (uint16_t i = 0; i < m->long_count && same == NULL; i++) {
        nb_mem_record_t *o = &m->longs[i];
        if (o->kind != kind) {
            continue;
        }
        if (r.key[0] != '\0' ? same_text(o->key, r.key) : same_text(o->text, r.text)) {
            same = o;
        }
    }
    nb_mem_status_t result = NB_MEM_OK;
    if (same != NULL) {
        memcpy(same->text, r.text, sizeof(same->text));
        same->uses++;
        same->when = when;
        if (id != NULL) {
            *id = same->id;
        }
        result = NB_MEM_UPDATED;
    } else {
        if (m->long_count >= NB_MEM_LONG_MAX) {
            return NB_MEM_FULL;
        }
        r.id = m->next_id++;
        r.kind = kind;
        r.when = when;
        m->longs[m->long_count++] = r;
        if (id != NULL) {
            *id = r.id;
        }
    }
    const nb_mem_status_t s = write_snapshot(m, io, true);
    return s == NB_MEM_OK ? result : s;
}

nb_mem_status_t nb_memory_correct(nb_memory_t *m, const nb_mem_io_t *io, uint32_t id,
                                  const char *text) {
    nb_mem_record_t *r = find_long(m, id);
    if (r == NULL) {
        r = find_episode(m, id);
    }
    if (r == NULL) {
        return NB_MEM_NOT_FOUND;
    }
    char clean[NB_MEM_TEXT_MAX];
    if (copy_text(clean, sizeof(clean), text) == 0 && r->kind != NB_MEM_EPISODE) {
        return NB_MEM_INVALID;
    }
    memcpy(r->text, clean, sizeof(r->text));
    // Both slots get the new text, so a damaged newer slot cannot bring the
    // old one back. An episode's old text may also sit in a log frame: the
    // snapshot already holds the episode, so the log goes.
    nb_mem_status_t s = write_snapshot(m, io, true);
    if (s == NB_MEM_OK) {
        s = write_snapshot(m, io, true);
    }
    if (s == NB_MEM_OK && r->kind == NB_MEM_EPISODE) {
        if (m->locked || !io->clear_log(io->ctx)) {
            return NB_MEM_IO;
        }
        m->log_frames = 0;
    }
    return s;
}

static float importance_now(const nb_mem_record_t *ep, const nb_clock_t *clock,
                            uint64_t now_mono_ms, bool *known) {
    int64_t age = 0;
    const bool k = clock != NULL && nb_clock_age_s(clock, &ep->when, now_mono_ms, &age);
    if (known != NULL) {
        *known = k;
    }
    if (!k) {
        return ep->importance;
    }
    const double hl = (double)kEpisodeTypes[ep->type].half_life_s;
    return (float)((double)ep->importance * exp2(-(double)age / hl));
}

float nb_memory_importance(const nb_mem_record_t *ep, const nb_clock_t *clock,
                           uint64_t now_mono_ms, bool *known) {
    return importance_now(ep, clock, now_mono_ms, known);
}

nb_mem_status_t nb_memory_episode(nb_memory_t *m, const nb_mem_io_t *io,
                                  const nb_mem_episode_t *ep, nb_stamp_t when,
                                  const nb_clock_t *clock, uint64_t now_mono_ms, uint32_t *id) {
    if (ep == NULL || (unsigned)ep->type >= NB_EP_TYPE_COUNT ||
        (ep->about != 0 && find_long(m, ep->about) == NULL)) {
        return NB_MEM_INVALID;
    }
    nb_mem_record_t r;
    memset(&r, 0, sizeof(r));
    r.id = m->next_id++;
    r.kind = NB_MEM_EPISODE;
    r.when = when;
    r.type = ep->type;
    r.valence = clampf(ep->valence, -1.0f, 1.0f);
    r.arousal = clampf(ep->arousal, -1.0f, 1.0f);
    r.importance = ep->importance > 0.0f ? clampf(ep->importance, 0.0f, 1.0f)
                                         : kEpisodeTypes[ep->type].importance;
    r.about = ep->about;
    copy_text(r.text, sizeof(r.text), ep->text);

    if (m->episode_count >= NB_MEM_EPISODE_MAX) {
        uint16_t least = 0;
        float least_imp = 2.0f;
        for (uint16_t i = 0; i < m->episode_count; i++) {
            const float imp = importance_now(&m->episodes[i], clock, now_mono_ms, NULL);
            if (imp < least_imp) {  // strict: the oldest wins a tie
                least_imp = imp;
                least = i;
            }
        }
        consolidate(m, &m->episodes[least]);
        remove_at(m->episodes, &m->episode_count, least);
    }
    insert_episode(m, &r);
    if (id != NULL) {
        *id = r.id;
    }

    nb_json_writer_t w;
    nb_jw_init(&w, m->scratch, m->scratch_cap);
    nb_jw_object_begin(&w);
    nb_jw_key(&w, "schema");
    nb_jw_int(&w, NB_MEM_SCHEMA);
    nb_jw_key(&w, "episode");
    write_record(&w, &r);
    nb_jw_object_end(&w);
    const size_t n = nb_jw_finish(&w);
    if (n == 0 || m->locked || !io->append(io->ctx, m->next_seq, m->scratch, (uint32_t)n)) {
        m->dirty = true;  // the next snapshot carries it
        return NB_MEM_IO;
    }
    m->next_seq++;
    m->log_frames++;
    return NB_MEM_OK;
}

// --- Forgetting -------------------------------------------------------------

static bool add_pending(nb_memory_t *m, uint32_t id) {
    if (is_pending(m, id)) {
        return true;
    }
    if (m->pending_count >= NB_MEM_PENDING_MAX) {
        return false;
    }
    m->pending[m->pending_count++] = id;
    return true;
}

// Which records a forget takes, decided before touching anything.
typedef bool (*forget_pick_t)(const nb_mem_record_t *r, const void *arg);

// takes_patterns: the counts go too, since they came from the episodes.
static nb_mem_status_t forget_where(nb_memory_t *m, const nb_mem_io_t *io, forget_pick_t pick,
                                    const void *arg, bool takes_patterns, nb_mem_forgot_t *out) {
    nb_mem_forgot_t gone;
    memset(&gone, 0, sizeof(gone));
    const uint32_t counted = takes_patterns ? patterns_all(&m->patterns) : 0;
    uint32_t longs_taken[NB_MEM_LONG_MAX];
    uint16_t nlong = 0;
    for (uint16_t i = 0; i < m->long_count; i++) {
        if (pick(&m->longs[i], arg)) {
            longs_taken[nlong++] = m->longs[i].id;
        }
    }
    uint16_t neps = 0;
    for (uint16_t i = 0; i < m->episode_count; i++) {
        const nb_mem_record_t *e = &m->episodes[i];
        bool about_taken = false;
        for (uint16_t j = 0; j < nlong && !about_taken; j++) {
            about_taken = e->about == longs_taken[j];
        }
        if (pick(e, arg) || about_taken) {
            neps++;
        }
    }
    if (nlong + neps == 0 && counted == 0) {
        if (out != NULL) {
            *out = gone;
        }
        // A forget left half done earlier still gets finished.
        return m->pending_count > 0 ? (finish_forget(m, io) == NB_MEM_OK ? NB_MEM_NOT_FOUND
                                                                          : NB_MEM_IO)
                                    : NB_MEM_NOT_FOUND;
    }
    if (m->pending_count + nlong + neps > NB_MEM_PENDING_MAX) {
        const nb_mem_status_t s = finish_forget(m, io);  // make room in the mark
        if (s != NB_MEM_OK) {
            return s;
        }
    }

    for (uint16_t i = m->episode_count; i-- > 0;) {
        const nb_mem_record_t *e = &m->episodes[i];
        bool about_taken = false;
        for (uint16_t j = 0; j < nlong && !about_taken; j++) {
            about_taken = e->about == longs_taken[j];
        }
        if (pick(e, arg) || about_taken) {
            add_pending(m, e->id);
            gone.count[NB_MEM_EPISODE]++;
            remove_at(m->episodes, &m->episode_count, i);
        }
    }
    for (uint16_t i = m->long_count; i-- > 0;) {
        if (pick(&m->longs[i], arg)) {
            add_pending(m, m->longs[i].id);
            gone.count[m->longs[i].kind]++;
            remove_at(m->longs, &m->long_count, i);
        }
    }
    if (counted > 0) {
        memset(&m->patterns, 0, sizeof(m->patterns));
        gone.counted = counted;
    }
    if (out != NULL) {
        *out = gone;
    }
    m->dirty = true;
    return finish_forget(m, io);
}

static bool pick_id(const nb_mem_record_t *r, const void *arg) {
    return r->id == *(const uint32_t *)arg;
}

static bool pick_kind(const nb_mem_record_t *r, const void *arg) {
    return r->kind == *(const nb_mem_kind_t *)arg;
}

static bool pick_all(const nb_mem_record_t *r, const void *arg) {
    (void)r;
    (void)arg;
    return true;
}

nb_mem_status_t nb_memory_forget_id(nb_memory_t *m, const nb_mem_io_t *io, uint32_t id,
                                    nb_mem_forgot_t *out) {
    return forget_where(m, io, pick_id, &id, false, out);
}

nb_mem_status_t nb_memory_forget_kind(nb_memory_t *m, const nb_mem_io_t *io,
                                      nb_mem_kind_t kind, nb_mem_forgot_t *out) {
    if ((unsigned)kind >= NB_MEM_KIND_COUNT) {
        return NB_MEM_INVALID;
    }
    return forget_where(m, io, pick_kind, &kind, kind == NB_MEM_EPISODE, out);
}

nb_mem_status_t nb_memory_forget_all(nb_memory_t *m, const nb_mem_io_t *io,
                                     nb_mem_forgot_t *out) {
    return forget_where(m, io, pick_all, NULL, true, out);
}

// --- Upkeep -----------------------------------------------------------------

nb_mem_status_t nb_memory_flush(nb_memory_t *m, const nb_mem_io_t *io) {
    if (m->pending_count > 0) {
        return finish_forget(m, io);
    }
    return m->dirty ? write_snapshot(m, io, false) : NB_MEM_OK;
}

bool nb_memory_wants_compaction(const nb_memory_t *m) {
    return m->log_frames >= NB_MEM_COMPACT_FRAMES;
}

nb_mem_status_t nb_memory_compact(nb_memory_t *m, const nb_mem_io_t *io,
                                  const nb_clock_t *clock, uint64_t now_mono_ms) {
    for (uint16_t i = m->episode_count; i-- > 0;) {
        bool known;
        const float imp = importance_now(&m->episodes[i], clock, now_mono_ms, &known);
        if (known && imp < NB_MEM_FADED) {
            consolidate(m, &m->episodes[i]);
            remove_at(m->episodes, &m->episode_count, i);
        }
    }
    if (m->pending_count > 0) {
        return finish_forget(m, io);
    }
    const nb_mem_status_t s = write_snapshot(m, io, false);
    if (s != NB_MEM_OK) {
        return s;
    }
    if (!io->clear_log(io->ctx)) {
        return NB_MEM_IO;
    }
    m->log_frames = 0;
    return NB_MEM_OK;
}

bool nb_memory_rebase(nb_memory_t *m, const nb_clock_t *clock) {
    bool changed = false;
    for (uint16_t i = 0; i < m->long_count; i++) {
        changed |= nb_clock_rebase(clock, &m->longs[i].when);
    }
    for (uint16_t i = 0; i < m->episode_count; i++) {
        changed |= nb_clock_rebase(clock, &m->episodes[i].when);
    }
    if (changed) {
        m->dirty = true;
    }
    return changed;
}

void nb_memory_bias(const nb_memory_t *m, const nb_clock_t *clock, uint64_t now_mono_ms,
                    float *valence, float *arousal) {
    float v = 0.0f, a = 0.0f;
    for (uint16_t i = 0; i < m->episode_count; i++) {
        bool known;
        const float imp = importance_now(&m->episodes[i], clock, now_mono_ms, &known);
        if (known) {
            v += imp * m->episodes[i].valence;
            a += imp * m->episodes[i].arousal;
        }
    }
    *valence = clampf(v * NB_MEM_BIAS_GAIN, -NB_MEM_BIAS_MAX, NB_MEM_BIAS_MAX);
    *arousal = clampf(a * NB_MEM_BIAS_GAIN, -NB_MEM_BIAS_MAX, NB_MEM_BIAS_MAX);
}

// --- Recall -----------------------------------------------------------------

#define MAX_WORDS 8
#define WORD_MAX 32

static int split_words(const char *words, char out[MAX_WORDS][WORD_MAX]) {
    char folded[160];
    fold(words != NULL ? words : "", folded, sizeof(folded));
    int n = 0;
    const char *p = folded;
    while (*p != '\0' && n < MAX_WORDS) {
        while (*p != '\0' && !((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                               (unsigned char)*p >= 0x80)) {
            p++;
        }
        const char *start = p;
        while ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
               (unsigned char)*p >= 0x80) {
            p++;
        }
        const size_t len = (size_t)(p - start);
        if (len >= 3 && len < WORD_MAX) {
            memcpy(out[n], start, len);
            out[n][len] = '\0';
            n++;
        }
    }
    return n;
}

static void consider(nb_mem_record_t *r, const nb_mem_query_t *q, const nb_clock_t *clock,
                     uint64_t now_mono_ms, char words[MAX_WORDS][WORD_MAX], int nwords,
                     nb_mem_hit_t *hits, int max_hits, int *nhits) {
    if (q->kind >= 0 && (int)r->kind != q->kind) {
        return;
    }
    if (q->max_age_s > 0) {
        int64_t age;
        if (clock == NULL || !nb_clock_age_s(clock, &r->when, now_mono_ms, &age) ||
            age > q->max_age_s) {
            return;
        }
    }
    float score = 0.0f;
    if (nwords > 0) {
        char hay[NB_MEM_KEY_MAX + NB_MEM_TEXT_MAX + 1];
        char folded[sizeof(hay)];
        snprintf(hay, sizeof(hay), "%s %s", r->key, r->text);
        fold(hay, folded, sizeof(folded));
        int matched = 0;
        for (int i = 0; i < nwords; i++) {
            matched += strstr(folded, words[i]) != NULL;
        }
        if (matched == 0) {
            return;
        }
        score = (float)matched;
    }
    // Within the same word count: long records by use, episodes by how much
    // they matter now; both stay below one word's worth.
    if (r->kind == NB_MEM_EPISODE) {
        score += 0.5f * importance_now(r, clock, now_mono_ms, NULL);
    } else {
        score += 0.5f + 0.4f * clampf((float)r->uses / 10.0f, 0.0f, 1.0f);
    }

    int pos = *nhits;
    if (pos >= max_hits) {
        if (hits[max_hits - 1].score >= score) {
            return;
        }
        pos = max_hits - 1;
    } else {
        (*nhits)++;
    }
    while (pos > 0 && hits[pos - 1].score < score) {
        hits[pos] = hits[pos - 1];
        pos--;
    }
    hits[pos].record = r;
    hits[pos].score = score;
}

int nb_memory_recall(nb_memory_t *m, const nb_mem_query_t *q, const nb_clock_t *clock,
                     uint64_t now_mono_ms, nb_mem_hit_t *hits, int max_hits) {
    if (q == NULL || hits == NULL || max_hits <= 0) {
        return 0;
    }
    char words[MAX_WORDS][WORD_MAX];
    const int nwords = split_words(q->words, words);
    if (q->words != NULL && q->words[0] != '\0' && nwords == 0) {
        return 0;  // only short words: nothing to look for
    }
    int n = 0;
    for (uint16_t i = 0; i < m->long_count; i++) {
        consider(&m->longs[i], q, clock, now_mono_ms, words, nwords, hits, max_hits, &n);
    }
    for (uint16_t i = m->episode_count; i-- > 0;) {  // newest first on ties
        consider(&m->episodes[i], q, clock, now_mono_ms, words, nwords, hits, max_hits, &n);
    }
    for (int i = 0; i < n; i++) {
        if (hits[i].record->kind != NB_MEM_EPISODE) {
            ((nb_mem_record_t *)hits[i].record)->uses++;
            m->dirty = true;
        }
    }
    return n;
}
