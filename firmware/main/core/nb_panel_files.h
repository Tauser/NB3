#ifndef NB_PANEL_FILES_H
#define NB_PANEL_FILES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What the web panel may do with the files of the memory folder on the card.
// The robot keeps its memory there in CRC-checked slots and logs; a stray
// delete or overwrite from a browser could corrupt them, so the panel can
// read and list everything but may only write or delete the sounds in sons/.
// personalidade.ini stays read only on purpose: the card layout keeps it clear
// of every write (memory/store_service). Pure core: paths come in as text.

typedef enum {
    NB_PANEL_READ = 0,
    NB_PANEL_WRITE,
    NB_PANEL_DELETE,
} nb_panel_op_t;

#define NB_PANEL_PATH_MAX 64u
#define NB_PANEL_SOUND_MAX_BYTES (64u * 1024u)   // same limit as behavior/sound_bank

// A relative path of one or two segments of lowercase letters, digits, '_', '-'
// and '.', with no '..', no leading '/' and no empty or dot-only segment.
bool nb_panel_path_valid(const char *path);

// Whether `op` is allowed on `path`. An invalid path is never allowed.
bool nb_panel_allowed(const char *path, nb_panel_op_t op);

// The most a write to `path` may carry; 0 where writing is not allowed.
uint32_t nb_panel_max_write_bytes(const char *path);

#ifdef __cplusplus
}
#endif

#endif  // NB_PANEL_FILES_H
