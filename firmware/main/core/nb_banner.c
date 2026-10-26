#include "nb_banner.h"

#include <string.h>

// Length of the well-formed UTF-8 character starting at s[0], or 0 when it is
// malformed or cut short by the terminator.
static size_t utf8_char_len(const unsigned char *s) {
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

// Copies `src` into dst (capacity `cap`, terminator included), never splitting
// a character.
static void copy_text(char *dst, size_t cap, const char *src) {
    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p != '\0') {
        size_t len = utf8_char_len(p);
        if (len == 0 || out + len > cap - 1) {
            break;
        }
        if (len == 1 && *p < 0x20 && *p != '\n') {
            dst[out++] = ' ';
        } else {
            memcpy(dst + out, p, len);
            out += len;
        }
        p += len;
    }
    dst[out] = '\0';
}

static bool is_sticky(nb_banner_kind_t kind) {
    return kind == NB_BANNER_WIFI_SETUP || kind == NB_BANNER_ACTIVATION;
}

void nb_banner_init(nb_banner_t *banner) {
    memset(banner, 0, sizeof(*banner));
}

bool nb_banner_show(nb_banner_t *banner, nb_banner_kind_t kind, const char *text,
                    uint32_t timeout_ms, uint32_t now_ms) {
    if (text == NULL || kind <= NB_BANNER_NONE || kind > NB_BANNER_ACTIVATION) {
        return false;
    }
    // A lower-priority message only loses to one that is still alive.
    if (banner->kind > kind && nb_banner_update(banner, now_ms)) {
        return false;
    }

    char copy[NB_BANNER_TEXT_MAX];
    copy_text(copy, sizeof(copy), text);
    if (copy[0] == '\0') {
        return false;
    }

    uint32_t timeout = 0;
    if (!is_sticky(kind)) {
        timeout = timeout_ms != 0 ? timeout_ms
                                  : (kind == NB_BANNER_ERROR ? NB_BANNER_ERROR_DEFAULT_MS
                                                             : NB_BANNER_NOTICE_DEFAULT_MS);
        if (timeout > NB_BANNER_MAX_TIMEOUT_MS) {
            timeout = NB_BANNER_MAX_TIMEOUT_MS;
        }
    }

    const bool same = banner->kind == kind && strcmp(banner->text, copy) == 0;
    banner->kind = kind;
    memcpy(banner->text, copy, sizeof(banner->text));
    banner->start_ms = now_ms;
    banner->timeout_ms = timeout;
    if (!same) {
        banner->revision++;
    }
    return true;
}

void nb_banner_clear(nb_banner_t *banner) {
    if (banner->kind == NB_BANNER_NONE) {
        return;
    }
    banner->kind = NB_BANNER_NONE;
    banner->text[0] = '\0';
    banner->timeout_ms = 0;
    banner->revision++;
}

void nb_banner_clear_kind(nb_banner_t *banner, nb_banner_kind_t kind) {
    if (banner->kind == kind) {
        nb_banner_clear(banner);
    }
}

bool nb_banner_update(nb_banner_t *banner, uint32_t now_ms) {
    if (banner->kind == NB_BANNER_NONE) {
        return false;
    }
    if (banner->timeout_ms != 0 && (uint32_t)(now_ms - banner->start_ms) >= banner->timeout_ms) {
        nb_banner_clear(banner);
        return false;
    }
    return true;
}
