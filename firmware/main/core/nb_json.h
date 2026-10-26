#ifndef NB_JSON_H
#define NB_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Small JSON writer and reader for the memory records (D-012). The cores
// stay free of third-party code so the host tests build anywhere; the data
// is small and its schema fixed, so a token array is enough.

// --- Writer ---------------------------------------------------------------
// Appends into a caller buffer. Any overflow makes ok false and the rest of
// the calls no-ops, so callers check once at the end.

#define NB_JSON_MAX_DEPTH 16

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool ok;
    uint8_t depth;
    bool after_key;
    bool first[NB_JSON_MAX_DEPTH];
} nb_json_writer_t;

void nb_jw_init(nb_json_writer_t *w, char *buf, size_t cap);
void nb_jw_object_begin(nb_json_writer_t *w);
void nb_jw_object_end(nb_json_writer_t *w);
void nb_jw_array_begin(nb_json_writer_t *w);
void nb_jw_array_end(nb_json_writer_t *w);
void nb_jw_key(nb_json_writer_t *w, const char *key);
void nb_jw_string(nb_json_writer_t *w, const char *s);
void nb_jw_int(nb_json_writer_t *w, int64_t v);
// Three decimals: enough for affect values and importance.
void nb_jw_float(nb_json_writer_t *w, float v);
void nb_jw_bool(nb_json_writer_t *w, bool v);
// Terminates the text; returns its length, or 0 if anything did not fit.
size_t nb_jw_finish(nb_json_writer_t *w);

// --- Reader ---------------------------------------------------------------

typedef enum {
    NB_JSON_OBJECT = 1,
    NB_JSON_ARRAY,
    NB_JSON_STRING,
    NB_JSON_NUMBER,
    NB_JSON_TRUE,
    NB_JSON_FALSE,
    NB_JSON_NULL,
} nb_json_type_t;

typedef struct {
    nb_json_type_t type;
    uint32_t start, end;  // bytes; a string's are inside the quotes, still escaped
    uint32_t size;        // children: keys of an object, items of an array
    uint32_t next;        // index of the first token after this one's subtree
} nb_json_tok_t;

// Tokens in document order; an object's children alternate key, value.
// Returns the token count, or -1 for malformed text, too many tokens, too
// deep nesting, or anything after the top value but whitespace.
int nb_json_parse(const char *text, size_t len, nb_json_tok_t *toks, int max_toks);

// Value of `key` in the object at index obj, or -1.
int nb_json_find(const char *text, const nb_json_tok_t *toks, int obj, const char *key);

// Unescaped string into out (terminated). False if not a string, badly
// escaped, holding a control character (NUL included), or longer than
// cap - 1.
bool nb_json_string(const char *text, const nb_json_tok_t *tok, char *out, size_t cap);
bool nb_json_number(const char *text, const nb_json_tok_t *tok, double *out);
// Integral numbers within [min, max] only.
bool nb_json_int(const char *text, const nb_json_tok_t *tok, int64_t min, int64_t max,
                 int64_t *out);
bool nb_json_bool(const nb_json_tok_t *tok, bool *out);

#ifdef __cplusplus
}
#endif

#endif  // NB_JSON_H
