#include "nb_personality.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// Safe ranges. Outside them the robot would look broken rather than different.
#define NB_PERS_TEMPERAMENT_MIN -0.5f
#define NB_PERS_TEMPERAMENT_MAX 0.5f
#define NB_PERS_REACTIVITY_MIN 0.25f
#define NB_PERS_REACTIVITY_MAX 2.0f
#define NB_PERS_RECOVERY_MIN_MS 20000u
#define NB_PERS_RECOVERY_MAX_MS 600000u
#define NB_PERS_SLOWDOWN_MIN 1.0f
#define NB_PERS_SLOWDOWN_MAX 4.0f
#define NB_PERS_LINE_MAX 96

nb_personality_t nb_personality_default(void) {
    nb_personality_t p;
    const nb_affect_personality_t a = nb_affect_default_personality();
    p.temperament = a.temperament;
    p.reactivity = a.reactivity;
    p.recovery_ms = a.recovery_ms;
    p.negative_slowdown = a.negative_slowdown;
    p.initiative = 0.5f;
    p.adaptation = 1.0f;
    return p;
}

static bool clamp_float(float *v, float lo, float hi) {
    const float before = *v;
    *v = *v < lo ? lo : (*v > hi ? hi : *v);
    return *v != before;
}

bool nb_personality_clamp(nb_personality_t *p) {
    if (p == NULL) {
        return false;
    }
    bool changed = false;
    changed |= clamp_float(&p->temperament.valence, NB_PERS_TEMPERAMENT_MIN, NB_PERS_TEMPERAMENT_MAX);
    changed |= clamp_float(&p->temperament.arousal, NB_PERS_TEMPERAMENT_MIN, NB_PERS_TEMPERAMENT_MAX);
    changed |= clamp_float(&p->reactivity, NB_PERS_REACTIVITY_MIN, NB_PERS_REACTIVITY_MAX);
    changed |= clamp_float(&p->negative_slowdown, NB_PERS_SLOWDOWN_MIN, NB_PERS_SLOWDOWN_MAX);
    changed |= clamp_float(&p->initiative, 0.0f, 1.0f);
    changed |= clamp_float(&p->adaptation, 0.0f, 1.0f);
    const uint32_t before = p->recovery_ms;
    p->recovery_ms = p->recovery_ms < NB_PERS_RECOVERY_MIN_MS ? NB_PERS_RECOVERY_MIN_MS
                     : p->recovery_ms > NB_PERS_RECOVERY_MAX_MS ? NB_PERS_RECOVERY_MAX_MS
                                                                : p->recovery_ms;
    changed |= p->recovery_ms != before;
    return changed;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

// Parses a whole token as a float; false on junk or an empty value.
static bool parse_float(const char *s, float *out) {
    char *end = NULL;
    const float v = strtof(s, &end);
    if (end == s || *trim(end) != '\0' || v != v) {
        return false;
    }
    *out = v;
    return true;
}

// Applies one key that known() accepted.
static void apply(nb_personality_t *p, const char *key, float v) {
    if (strcmp(key, "temperament_valence") == 0) {
        p->temperament.valence = v;
    } else if (strcmp(key, "temperament_arousal") == 0) {
        p->temperament.arousal = v;
    } else if (strcmp(key, "reactivity") == 0) {
        p->reactivity = v;
    } else if (strcmp(key, "recovery_s") == 0) {
        p->recovery_ms = v <= 0.0f ? 0u : (v >= 4.0e6f ? 4000000000u : (uint32_t)(v * 1000.0f));
    } else if (strcmp(key, "negative_slowdown") == 0) {
        p->negative_slowdown = v;
    } else if (strcmp(key, "initiative") == 0) {
        p->initiative = v;
    } else if (strcmp(key, "adaptation") == 0) {
        p->adaptation = v;
    }
}

static bool known(const char *key) {
    static const char *const kKeys[] = {"temperament_valence", "temperament_arousal",
                                        "reactivity",          "recovery_s",
                                        "negative_slowdown",   "initiative",
                                        "adaptation"};
    for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
        if (strcmp(key, kKeys[i]) == 0) {
            return true;
        }
    }
    return false;
}

size_t nb_personality_text(const uint8_t *bytes, size_t len, char *out, size_t cap) {
    if (out == NULL || cap == 0) {
        return 0;
    }
    size_t n = 0;
    if (bytes == NULL) {
        out[0] = '\0';
        return 0;
    }
    // 0 = UTF-8, 1 = UTF-16LE, 2 = UTF-16BE.
    int wide = 0;
    size_t at = 0;
    if (len >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        wide = 1;
        at = 2;
    } else if (len >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        wide = 2;
        at = 2;
    } else if (len >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        at = 3;
    } else if (len >= 2 && bytes[0] != 0 && bytes[1] == 0) {
        wide = 1;
    } else if (len >= 2 && bytes[0] == 0 && bytes[1] != 0) {
        wide = 2;
    }
    while (at < len && n + 1 < cap) {
        unsigned c;
        if (wide == 0) {
            c = bytes[at];
            at++;
            if (c >= 0x80) {
                while (at < len && (bytes[at] & 0xC0) == 0x80) {
                    at++;  // the rest of a multi-byte character
                }
                c = '?';
            }
        } else {
            if (at + 1 >= len) {
                break;
            }
            c = wide == 1 ? (unsigned)bytes[at] | ((unsigned)bytes[at + 1] << 8)
                          : ((unsigned)bytes[at] << 8) | (unsigned)bytes[at + 1];
            at += 2;
            if (c >= 0x80) {
                c = '?';
            }
        }
        if (c != 0) {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
    return n;
}

nb_personality_parse_result_t nb_personality_parse(const char *text, nb_personality_t *p) {
    nb_personality_parse_result_t r = {0, 0, 0, 0};
    if (text == NULL || p == NULL) {
        return r;
    }
    nb_personality_clamp(p);  // so each clamp below speaks only for its own key
    const char *line = text;
    while (*line != '\0') {
        const char *eol = strchr(line, '\n');
        size_t len = eol != NULL ? (size_t)(eol - line) : strlen(line);
        char buf[NB_PERS_LINE_MAX];
        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;  // overlong lines are cut; the key still reads
        }
        memcpy(buf, line, len);
        buf[len] = '\0';
        line = eol != NULL ? eol + 1 : line + strlen(line);

        char *hash = strchr(buf, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *eq = strchr(buf, '=');
        if (eq == NULL) {
            continue;  // blank, comment or not a setting
        }
        *eq = '\0';
        const char *key = trim(buf);
        const char *value = trim(eq + 1);
        if (!known(key)) {
            r.unknown++;
            continue;
        }
        float v;
        if (!parse_float(value, &v)) {
            r.rejected++;
            continue;
        }
        apply(p, key, v);
        if (nb_personality_clamp(p)) {
            r.clamped++;
        }
        r.applied++;
    }
    return r;
}

nb_affect_personality_t nb_personality_affect(const nb_personality_t *p) {
    nb_affect_personality_t a = nb_affect_default_personality();
    if (p != NULL) {
        a.temperament = p->temperament;
        a.reactivity = p->reactivity;
        a.recovery_ms = p->recovery_ms;
        a.negative_slowdown = p->negative_slowdown;
    }
    return a;
}
