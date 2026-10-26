#ifndef NB_BANNER_H
#define NB_BANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// System banner: the one piece of text NB3 puts over the avatar. It decides
// whether a message is worth showing and for how long; drawing is the
// shell's job. The enum order is the priority order: a message never
// replaces a higher-priority one that is still showing.
typedef enum {
    NB_BANNER_NONE = 0,
    NB_BANNER_NOTICE,      // plain notification, short timeout
    NB_BANNER_ERROR,       // network/server error, timeout
    NB_BANNER_WIFI_SETUP,  // Wi-Fi provisioning instructions, sticky
    NB_BANNER_ACTIVATION,  // device activation code, sticky
} nb_banner_kind_t;

#define NB_BANNER_TEXT_MAX 192  // bytes, including the terminator
#define NB_BANNER_NOTICE_DEFAULT_MS 3000u
#define NB_BANNER_ERROR_DEFAULT_MS 8000u
#define NB_BANNER_MAX_TIMEOUT_MS 60000u

typedef struct {
    nb_banner_kind_t kind;  // NB_BANNER_NONE when nothing is showing
    char text[NB_BANNER_TEXT_MAX];
    uint32_t start_ms;
    uint32_t timeout_ms;  // 0 = sticky (until replaced or cleared)
    uint32_t revision;    // bumped whenever kind or text changes
} nb_banner_t;

void nb_banner_init(nb_banner_t *banner);

// Shows `text` as `kind`. `timeout_ms` only applies to NOTICE/ERROR kinds
// (0 = the kind's default, capped at NB_BANNER_MAX_TIMEOUT_MS); the setup and
// activation kinds are always sticky. Returns false (state untouched) for an
// empty text, a NONE kind, or when a higher-priority message is showing.
// The text is copied and truncated on a UTF-8 character boundary; control
// characters other than '\n' become spaces and malformed sequences end the
// text.
bool nb_banner_show(nb_banner_t *banner, nb_banner_kind_t kind, const char *text,
                    uint32_t timeout_ms, uint32_t now_ms);

// Drops the banner whatever its kind.
void nb_banner_clear(nb_banner_t *banner);

// Drops the banner only if it is of `kind`.
void nb_banner_clear_kind(nb_banner_t *banner, nb_banner_kind_t kind);

// Expires a timed banner (wraparound-safe). Returns true while one is showing.
bool nb_banner_update(nb_banner_t *banner, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif  // NB_BANNER_H
