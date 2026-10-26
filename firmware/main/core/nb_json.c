#include "nb_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Writer ---------------------------------------------------------------

static void put(nb_json_writer_t *w, const char *s, size_t n) {
    if (!w->ok) {
        return;
    }
    if (w->len + n >= w->cap) {  // keep room for the terminator
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->len, s, n);
    w->len += n;
}

// Comma between siblings; nothing between a key and its value.
static void before_value(nb_json_writer_t *w) {
    if (w->after_key) {
        w->after_key = false;
        return;
    }
    if (w->depth > 0) {
        if (!w->first[w->depth - 1]) {
            put(w, ",", 1);
        }
        w->first[w->depth - 1] = false;
    }
}

static void jw_open(nb_json_writer_t *w, char c) {
    before_value(w);
    if (w->depth >= NB_JSON_MAX_DEPTH) {
        w->ok = false;
        return;
    }
    put(w, &c, 1);
    w->first[w->depth++] = true;
}

static void jw_close(nb_json_writer_t *w, char c) {
    if (w->depth == 0 || w->after_key) {
        w->ok = false;
        return;
    }
    w->depth--;
    put(w, &c, 1);
}

void nb_jw_init(nb_json_writer_t *w, char *buf, size_t cap) {
    memset(w, 0, sizeof(*w));
    w->buf = buf;
    w->cap = cap;
    w->ok = buf != NULL && cap > 0;
}

void nb_jw_object_begin(nb_json_writer_t *w) { jw_open(w, '{'); }
void nb_jw_object_end(nb_json_writer_t *w) { jw_close(w, '}'); }
void nb_jw_array_begin(nb_json_writer_t *w) { jw_open(w, '['); }
void nb_jw_array_end(nb_json_writer_t *w) { jw_close(w, ']'); }

static void put_string(nb_json_writer_t *w, const char *s) {
    put(w, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        char esc[8];
        if (*p == '"' || *p == '\\') {
            esc[0] = '\\';
            esc[1] = (char)*p;
            put(w, esc, 2);
        } else if (*p < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", *p);
            put(w, esc, 6);
        } else {
            put(w, (const char *)p, 1);
        }
    }
    put(w, "\"", 1);
}

void nb_jw_key(nb_json_writer_t *w, const char *key) {
    if (w->after_key) {
        w->ok = false;
        return;
    }
    before_value(w);
    put_string(w, key);
    put(w, ":", 1);
    w->after_key = true;
}

void nb_jw_string(nb_json_writer_t *w, const char *s) {
    before_value(w);
    put_string(w, s != NULL ? s : "");
}

void nb_jw_int(nb_json_writer_t *w, int64_t v) {
    char tmp[24];
    before_value(w);
    const int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
    put(w, tmp, (size_t)n);
}

void nb_jw_float(nb_json_writer_t *w, float v) {
    char tmp[32];
    before_value(w);
    const int n = snprintf(tmp, sizeof(tmp), "%.3f", isfinite(v) ? (double)v : 0.0);
    put(w, tmp, (size_t)n);
}

void nb_jw_bool(nb_json_writer_t *w, bool v) {
    before_value(w);
    put(w, v ? "true" : "false", v ? 4 : 5);
}

size_t nb_jw_finish(nb_json_writer_t *w) {
    if (!w->ok || w->depth != 0 || w->after_key) {
        return 0;
    }
    w->buf[w->len] = '\0';
    return w->len;
}

// --- Reader ---------------------------------------------------------------

typedef struct {
    const char *s;
    size_t len;
    size_t pos;
    nb_json_tok_t *toks;
    int max;
    int count;
} parser_t;

static void skip_ws(parser_t *p) {
    while (p->pos < p->len) {
        const char c = p->s[p->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        p->pos++;
    }
}

static int new_token(parser_t *p, nb_json_type_t type, size_t start) {
    if (p->count >= p->max) {
        return -1;
    }
    nb_json_tok_t *t = &p->toks[p->count];
    t->type = type;
    t->start = (uint32_t)start;
    t->end = (uint32_t)start;
    t->size = 0;
    t->next = 0;
    return p->count++;
}

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool parse_string(parser_t *p) {
    const size_t start = ++p->pos;  // past the opening quote
    while (p->pos < p->len) {
        const unsigned char c = (unsigned char)p->s[p->pos];
        if (c == '"') {
            const int i = new_token(p, NB_JSON_STRING, start);
            if (i < 0) {
                return false;
            }
            p->toks[i].end = (uint32_t)p->pos;
            p->toks[i].next = (uint32_t)p->count;
            p->pos++;
            return true;
        }
        if (c < 0x20) {
            return false;
        }
        if (c == '\\') {
            if (++p->pos >= p->len) {
                return false;
            }
            const char e = p->s[p->pos];
            if (e == 'u') {
                for (int k = 1; k <= 4; k++) {
                    if (p->pos + (size_t)k >= p->len || !is_hex(p->s[p->pos + (size_t)k])) {
                        return false;
                    }
                }
                p->pos += 4;
            } else if (strchr("\"\\/bfnrt", e) == NULL || e == '\0') {
                return false;
            }
        }
        p->pos++;
    }
    return false;
}

static bool parse_number(parser_t *p) {
    const size_t start = p->pos;
    const char *s = p->s;
    size_t i = p->pos;
    if (i < p->len && s[i] == '-') {
        i++;
    }
    if (i >= p->len || !is_digit(s[i])) {
        return false;
    }
    if (s[i] == '0') {
        i++;
    } else {
        while (i < p->len && is_digit(s[i])) {
            i++;
        }
    }
    if (i < p->len && s[i] == '.') {
        i++;
        if (i >= p->len || !is_digit(s[i])) {
            return false;
        }
        while (i < p->len && is_digit(s[i])) {
            i++;
        }
    }
    if (i < p->len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < p->len && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        if (i >= p->len || !is_digit(s[i])) {
            return false;
        }
        while (i < p->len && is_digit(s[i])) {
            i++;
        }
    }
    const int t = new_token(p, NB_JSON_NUMBER, start);
    if (t < 0) {
        return false;
    }
    p->toks[t].end = (uint32_t)i;
    p->toks[t].next = (uint32_t)p->count;
    p->pos = i;
    return true;
}

static bool parse_literal(parser_t *p, const char *word, nb_json_type_t type) {
    const size_t n = strlen(word);
    if (p->len - p->pos < n || memcmp(p->s + p->pos, word, n) != 0) {
        return false;
    }
    const int t = new_token(p, type, p->pos);
    if (t < 0) {
        return false;
    }
    p->pos += n;
    p->toks[t].end = (uint32_t)p->pos;
    p->toks[t].next = (uint32_t)p->count;
    return true;
}

static bool parse_value(parser_t *p, int depth);

static bool parse_container(parser_t *p, int depth, bool object) {
    if (depth >= NB_JSON_MAX_DEPTH) {
        return false;
    }
    const int t = new_token(p, object ? NB_JSON_OBJECT : NB_JSON_ARRAY, p->pos);
    if (t < 0) {
        return false;
    }
    const char closer = object ? '}' : ']';
    p->pos++;
    skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == closer) {
        p->pos++;
    } else {
        for (;;) {
            skip_ws(p);
            if (object) {
                if (p->pos >= p->len || p->s[p->pos] != '"' || !parse_string(p)) {
                    return false;
                }
                skip_ws(p);
                if (p->pos >= p->len || p->s[p->pos] != ':') {
                    return false;
                }
                p->pos++;
            }
            if (!parse_value(p, depth + 1)) {
                return false;
            }
            p->toks[t].size++;
            skip_ws(p);
            if (p->pos >= p->len) {
                return false;
            }
            const char c = p->s[p->pos++];
            if (c == closer) {
                break;
            }
            if (c != ',') {
                return false;
            }
        }
    }
    p->toks[t].end = (uint32_t)p->pos;
    p->toks[t].next = (uint32_t)p->count;
    return true;
}

static bool parse_value(parser_t *p, int depth) {
    skip_ws(p);
    if (p->pos >= p->len) {
        return false;
    }
    switch (p->s[p->pos]) {
        case '{': return parse_container(p, depth, true);
        case '[': return parse_container(p, depth, false);
        case '"': return parse_string(p);
        case 't': return parse_literal(p, "true", NB_JSON_TRUE);
        case 'f': return parse_literal(p, "false", NB_JSON_FALSE);
        case 'n': return parse_literal(p, "null", NB_JSON_NULL);
        default: return parse_number(p);
    }
}

int nb_json_parse(const char *text, size_t len, nb_json_tok_t *toks, int max_toks) {
    if (text == NULL || toks == NULL || max_toks <= 0) {
        return -1;
    }
    parser_t p = {text, len, 0, toks, max_toks, 0};
    if (!parse_value(&p, 0)) {
        return -1;
    }
    skip_ws(&p);
    return p.pos == len ? p.count : -1;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return (c | 0x20) - 'a' + 10;
}

static uint32_t read_u16(const char *s) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v = (v << 4) | (uint32_t)hex_value(s[i]);
    }
    return v;
}

static size_t utf8_put(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

bool nb_json_string(const char *text, const nb_json_tok_t *tok, char *out, size_t cap) {
    if (tok == NULL || tok->type != NB_JSON_STRING || out == NULL || cap == 0) {
        return false;
    }
    size_t n = 0;
    for (uint32_t i = tok->start; i < tok->end; i++) {
        char piece[4];
        size_t plen = 1;
        piece[0] = text[i];
        if (text[i] == '\\') {
            const char e = text[++i];
            switch (e) {
                case 'b': piece[0] = '\b'; break;
                case 'f': piece[0] = '\f'; break;
                case 'n': piece[0] = '\n'; break;
                case 'r': piece[0] = '\r'; break;
                case 't': piece[0] = '\t'; break;
                case 'u': {
                    uint32_t cp = read_u16(text + i + 1);
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i + 6 >= tok->end || text[i + 1] != '\\' || text[i + 2] != 'u') {
                            return false;
                        }
                        const uint32_t lo = read_u16(text + i + 3);
                        if (lo < 0xDC00 || lo > 0xDFFF) {
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    if (cp < 0x20) {
                        return false;  // NUL and control characters are not text
                    }
                    plen = utf8_put(cp, piece);
                    break;
                }
                default: piece[0] = e; break;  // \" \\ \/
            }
            if (plen == 1 && (unsigned char)piece[0] < 0x20) {
                return false;
            }
        }
        if (n + plen > cap - 1) {
            return false;
        }
        memcpy(out + n, piece, plen);
        n += plen;
    }
    out[n] = '\0';
    return true;
}

bool nb_json_number(const char *text, const nb_json_tok_t *tok, double *out) {
    if (tok == NULL || tok->type != NB_JSON_NUMBER) {
        return false;
    }
    char tmp[40];
    const size_t n = tok->end - tok->start;
    if (n == 0 || n >= sizeof(tmp)) {
        return false;
    }
    memcpy(tmp, text + tok->start, n);
    tmp[n] = '\0';
    char *end = NULL;
    const double v = strtod(tmp, &end);
    if (end != tmp + n || !isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

bool nb_json_int(const char *text, const nb_json_tok_t *tok, int64_t min, int64_t max,
                 int64_t *out) {
    if (tok == NULL || tok->type != NB_JSON_NUMBER) {
        return false;
    }
    uint32_t i = tok->start;
    const bool negative = text[i] == '-';
    if (negative) {
        i++;
    }
    if (i >= tok->end) {
        return false;
    }
    uint64_t v = 0;
    for (; i < tok->end; i++) {
        if (!is_digit(text[i])) {
            return false;  // fraction or exponent
        }
        const uint64_t d = (uint64_t)(text[i] - '0');
        if (v > (UINT64_C(9223372036854775808) - d) / 10) {
            return false;
        }
        v = v * 10 + d;
    }
    int64_t r;
    if (negative) {
        r = v == UINT64_C(9223372036854775808) ? INT64_MIN : -(int64_t)v;
    } else {
        if (v > (uint64_t)INT64_MAX) {
            return false;
        }
        r = (int64_t)v;
    }
    if (r < min || r > max) {
        return false;
    }
    *out = r;
    return true;
}

bool nb_json_bool(const nb_json_tok_t *tok, bool *out) {
    if (tok == NULL || (tok->type != NB_JSON_TRUE && tok->type != NB_JSON_FALSE)) {
        return false;
    }
    *out = tok->type == NB_JSON_TRUE;
    return true;
}

int nb_json_find(const char *text, const nb_json_tok_t *toks, int obj, const char *key) {
    if (obj < 0 || toks[obj].type != NB_JSON_OBJECT) {
        return -1;
    }
    const size_t klen = strlen(key);
    int k = obj + 1;
    for (uint32_t i = 0; i < toks[obj].size; i++) {
        const nb_json_tok_t *kt = &toks[k];
        const int v = (int)kt->next;
        // Our keys never need escapes, so a raw compare is exact for them.
        if (kt->end - kt->start == klen && memcmp(text + kt->start, key, klen) == 0) {
            return v;
        }
        k = (int)toks[v].next;
    }
    return -1;
}
