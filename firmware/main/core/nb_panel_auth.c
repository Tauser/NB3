#include "nb_panel_auth.h"

#include <string.h>

static bool reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;  // safe across the 32-bit clock wrap
}

// Compares without stopping at the first difference.
static bool same_text(const char *a, const char *b, size_t length)
{
    unsigned char diff = 0;
    for (size_t i = 0; i < length; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

void nb_panel_auth_init(nb_panel_auth_t *auth)
{
    if (auth != NULL) {
        memset(auth, 0, sizeof(*auth));
    }
}

void nb_panel_auth_open(nb_panel_auth_t *auth, uint32_t random, uint32_t now_ms, uint32_t idle_ms)
{
    if (auth == NULL) {
        return;
    }
    memset(auth, 0, sizeof(*auth));
    for (int i = NB_PANEL_CODE_LEN - 1; i >= 0; i--) {
        auth->code[i] = (char)('0' + random % 10u);
        random /= 10u;
    }
    auth->open = true;
    auth->idle_ms = idle_ms;
    auth->deadline_ms = now_ms + idle_ms;
}

void nb_panel_auth_close(nb_panel_auth_t *auth)
{
    nb_panel_auth_init(auth);
}

bool nb_panel_auth_is_open(nb_panel_auth_t *auth, uint32_t now_ms)
{
    if (auth == NULL || !auth->open) {
        return false;
    }
    if (reached(now_ms, auth->deadline_ms)) {
        nb_panel_auth_close(auth);
        return false;
    }
    return true;
}

nb_panel_login_t nb_panel_auth_login(nb_panel_auth_t *auth, const char *code, uint32_t random_a,
                                     uint32_t random_b, uint32_t now_ms, char *token_out)
{
    if (!nb_panel_auth_is_open(auth, now_ms)) {
        return NB_PANEL_LOGIN_CLOSED;
    }
    if (code == NULL || strlen(code) != NB_PANEL_CODE_LEN ||
        !same_text(code, auth->code, NB_PANEL_CODE_LEN)) {
        if (++auth->wrong >= NB_PANEL_MAX_WRONG) {
            nb_panel_auth_close(auth);
            return NB_PANEL_LOGIN_CLOSED;
        }
        return NB_PANEL_LOGIN_WRONG;
    }
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < NB_PANEL_TOKEN_LEN; i++) {
        const uint32_t word = i < NB_PANEL_TOKEN_LEN / 2 ? random_a : random_b;
        const int shift = 4 * (i % (NB_PANEL_TOKEN_LEN / 2));
        auth->token[i] = digits[(word >> shift) & 0xFu];
    }
    auth->token[NB_PANEL_TOKEN_LEN] = '\0';
    auth->has_token = true;
    auth->wrong = 0;
    auth->deadline_ms = now_ms + auth->idle_ms;
    if (token_out != NULL) {
        memcpy(token_out, auth->token, NB_PANEL_TOKEN_LEN + 1);
    }
    return NB_PANEL_LOGIN_OK;
}

bool nb_panel_auth_check(nb_panel_auth_t *auth, const char *token, uint32_t now_ms)
{
    if (!nb_panel_auth_is_open(auth, now_ms) || !auth->has_token || token == NULL ||
        strlen(token) != NB_PANEL_TOKEN_LEN ||
        !same_text(token, auth->token, NB_PANEL_TOKEN_LEN)) {
        return false;
    }
    auth->deadline_ms = now_ms + auth->idle_ms;
    return true;
}
