#include "nb_panel_files.h"

#include <string.h>

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

bool nb_panel_path_valid(const char *path)
{
    if (path == NULL) {
        return false;
    }
    const size_t length = strlen(path);
    if (length == 0 || length >= NB_PANEL_PATH_MAX) {
        return false;
    }
    int segments = 1;
    size_t segment_length = 0;
    bool only_dots = true;
    for (size_t i = 0; i < length; i++) {
        const char c = path[i];
        if (c == '/') {
            if (segment_length == 0 || only_dots) {
                return false;  // leading or doubled '/', or "." and ".." segments
            }
            if (++segments > 2) {
                return false;
            }
            segment_length = 0;
            only_dots = true;
        } else if (name_char(c)) {
            segment_length++;
            if (c != '.') {
                only_dots = false;
            }
        } else {
            return false;
        }
    }
    return segment_length > 0 && !only_dots;
}

static bool is_sound(const char *path)
{
    static const char prefix[] = "sons/";
    static const char suffix[] = ".ogg";
    const size_t length = strlen(path);
    const size_t prefix_length = sizeof(prefix) - 1;
    const size_t suffix_length = sizeof(suffix) - 1;
    return length > prefix_length + suffix_length && strncmp(path, prefix, prefix_length) == 0 &&
           strchr(path + prefix_length, '/') == NULL &&
           strcmp(path + length - suffix_length, suffix) == 0;
}

bool nb_panel_allowed(const char *path, nb_panel_op_t op)
{
    if (!nb_panel_path_valid(path)) {
        return false;
    }
    switch (op) {
    case NB_PANEL_READ:
        return true;
    case NB_PANEL_WRITE:
        return is_sound(path) || strcmp(path, "personalidade.ini") == 0;
    case NB_PANEL_DELETE:
        return is_sound(path);
    }
    return false;
}

uint32_t nb_panel_max_write_bytes(const char *path)
{
    if (!nb_panel_allowed(path, NB_PANEL_WRITE)) {
        return 0;
    }
    return is_sound(path) ? NB_PANEL_SOUND_MAX_BYTES : NB_PANEL_INI_MAX_BYTES;
}
