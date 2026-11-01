#ifndef NB_PANIC_RECORD_H
#define NB_PANIC_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NB_PANIC_BACKTRACE_DEPTH 12

// What the panic handler leaves in RTC memory for the next boot. Panic
// output is easy to lose on the serial link (a filtered or reconnecting
// monitor); this keeps the crash site until the firmware can log it. The
// checksum tells a record written by the handler from RTC memory that was
// never initialised.
typedef struct {
    uint32_t magic;
    uint32_t checksum;
    uint32_t core;
    uint32_t exception;  // panic_exception_t
    uint32_t cause;      // architecture exception cause
    uint32_t pc;
    uint32_t excvaddr;
    uint32_t depth;
    uint32_t bt_pc[NB_PANIC_BACKTRACE_DEPTH];
    uint32_t bt_sp[NB_PANIC_BACKTRACE_DEPTH];
} nb_panic_record_t;

// Stamps the magic and checksum; the handler calls it after filling the rest.
void nb_panic_record_seal(nb_panic_record_t *rec);

// True only for a sealed record that has not been altered since.
int nb_panic_record_valid(const nb_panic_record_t *rec);

// Marks the record as consumed so one crash is reported once.
void nb_panic_record_clear(nb_panic_record_t *rec);

// Writes the crash site and a backtrace as "pc:sp" pairs, the form
// xtensa-esp-elf-addr2line accepts. Returns the length (truncated to size).
size_t nb_panic_record_format(const nb_panic_record_t *rec, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif // NB_PANIC_RECORD_H
