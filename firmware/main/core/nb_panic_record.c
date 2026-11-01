#include "nb_panic_record.h"

#include <stdio.h>
#include <string.h>

#define MAGIC 0x4e425043u  // "NBPC"

static uint32_t checksum_of(const nb_panic_record_t *rec) {
    const uint32_t *words = (const uint32_t *)rec;
    size_t count = sizeof(*rec) / sizeof(uint32_t);
    uint32_t sum = 0x9e3779b9u;
    // Words 0 and 1 are the magic and the checksum itself.
    for (size_t i = 2; i < count; i++) {
        sum = (sum << 5 | sum >> 27) ^ words[i];
    }
    return sum;
}

void nb_panic_record_seal(nb_panic_record_t *rec) {
    if (rec->depth > NB_PANIC_BACKTRACE_DEPTH) {
        rec->depth = NB_PANIC_BACKTRACE_DEPTH;
    }
    rec->magic = MAGIC;
    rec->checksum = checksum_of(rec);
}

int nb_panic_record_valid(const nb_panic_record_t *rec) {
    return rec->magic == MAGIC && rec->checksum == checksum_of(rec) &&
           rec->depth <= NB_PANIC_BACKTRACE_DEPTH;
}

void nb_panic_record_clear(nb_panic_record_t *rec) {
    memset(rec, 0, sizeof(*rec));
}

size_t nb_panic_record_format(const nb_panic_record_t *rec, char *out, size_t size) {
    if (size == 0) {
        return 0;
    }
    size_t len = 0;
    int n = snprintf(out, size, "core=%u exception=%u cause=%u pc=0x%08x excvaddr=0x%08x bt:",
                     (unsigned)rec->core, (unsigned)rec->exception, (unsigned)rec->cause,
                     (unsigned)rec->pc, (unsigned)rec->excvaddr);
    len = n < 0 ? 0 : (size_t)n;
    for (uint32_t i = 0; i < rec->depth && len < size; i++) {
        n = snprintf(out + len, size - len, " 0x%08x:0x%08x", (unsigned)rec->bt_pc[i],
                     (unsigned)rec->bt_sp[i]);
        if (n < 0) {
            break;
        }
        len += (size_t)n;
    }
    if (len >= size) {
        len = size - 1;
    }
    return len;
}
