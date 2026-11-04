#ifndef NB_PANEL_AUTH_H
#define NB_PANEL_AUTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Who may use the web panel. The panel opens on request with a short code
// shown on the robot's own screen; whoever types it gets a session token, and
// the token lasts while it is used. Too many wrong codes close the panel, so
// a guess cannot be repeated without asking the robot again. Pure core: the
// random numbers and the time come in as parameters.

#define NB_PANEL_CODE_LEN 6
#define NB_PANEL_TOKEN_LEN 16
#define NB_PANEL_MAX_WRONG 5

typedef struct {
    bool open;
    char code[NB_PANEL_CODE_LEN + 1];
    char token[NB_PANEL_TOKEN_LEN + 1];
    bool has_token;
    int wrong;
    uint32_t idle_ms;       // how long the panel and its token live without use
    uint32_t deadline_ms;
} nb_panel_auth_t;

typedef enum {
    NB_PANEL_LOGIN_OK = 0,
    NB_PANEL_LOGIN_WRONG,
    NB_PANEL_LOGIN_CLOSED,  // not open, expired, or closed by too many wrong codes
} nb_panel_login_t;

void nb_panel_auth_init(nb_panel_auth_t *auth);

// Opens the panel with a new code made from `random`, replacing any session.
void nb_panel_auth_open(nb_panel_auth_t *auth, uint32_t random, uint32_t now_ms,
                        uint32_t idle_ms);

void nb_panel_auth_close(nb_panel_auth_t *auth);

// Open and not idle for longer than idle_ms.
bool nb_panel_auth_is_open(nb_panel_auth_t *auth, uint32_t now_ms);

// Checks a typed code. On OK, `token_out` (NB_PANEL_TOKEN_LEN + 1 bytes) holds a
// new token made from the two random words.
nb_panel_login_t nb_panel_auth_login(nb_panel_auth_t *auth, const char *code, uint32_t random_a,
                                     uint32_t random_b, uint32_t now_ms, char *token_out);

// Whether `token` is the live session's; a good check keeps the panel open.
bool nb_panel_auth_check(nb_panel_auth_t *auth, const char *token, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif  // NB_PANEL_AUTH_H
