#ifndef NB_DIAG_PANIC_RECORD_H
#define NB_DIAG_PANIC_RECORD_H

#ifdef __cplusplus
extern "C" {
#endif

// Logs the reset reason and, when the previous run ended in a panic or
// watchdog reset, the crash site the panic handler kept in RTC memory. Call
// first thing in app_main.
void nb_panic_record_report_boot(void);

#ifdef __cplusplus
}
#endif

#endif
