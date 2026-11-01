#include "diag/panic_record.h"

#include <cstdint>

#include <esp_attr.h>
#include <esp_debug_helpers.h>
#include <esp_log.h>
#include <esp_private/panic_internal.h>
#include <esp_system.h>
#include <xtensa_context.h>

extern "C" {
#include "core/nb_panic_record.h"
}

#define TAG "PanicRecord"

// RTC no-init memory survives the software reset a panic ends in.
static RTC_NOINIT_ATTR nb_panic_record_t s_record;

extern "C" {

void __real_esp_panic_handler(panic_info_t *info);

// The linker routes esp_panic_handler here (--wrap in main/CMakeLists.txt).
// Runs in panic context with the flash cache possibly off: IRAM code only,
// no logging, no allocation.
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info) {
    nb_panic_record_t rec = {};
    const XtExcFrame *frame = (const XtExcFrame *)info->frame;
    rec.core = (uint32_t)info->core;
    rec.exception = (uint32_t)info->exception;
    if (frame != NULL) {
        rec.cause = frame->exccause;
        rec.pc = frame->pc;
        rec.excvaddr = frame->excvaddr;
        esp_backtrace_frame_t bt = {};
        bt.pc = frame->pc;
        bt.sp = frame->a1;
        bt.next_pc = frame->a0;
        bt.exc_frame = frame;
        while (rec.depth < NB_PANIC_BACKTRACE_DEPTH) {
            rec.bt_pc[rec.depth] = bt.pc;
            rec.bt_sp[rec.depth] = bt.sp;
            rec.depth++;
            if (!esp_backtrace_get_next_frame(&bt)) {
                break;
            }
        }
    }
    nb_panic_record_seal(&rec);
    s_record = rec;
    __real_esp_panic_handler(info);
}

}  // extern "C"

extern "C" void nb_panic_record_report_boot(void) {
    const esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", (int)reason);

    const bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                         reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
    if (nb_panic_record_valid(&s_record)) {
        char text[256];
        nb_panic_record_format(&s_record, text, sizeof(text));
        ESP_LOGE(TAG, "Previous run crashed: %s", text);
    } else if (crashed) {
        ESP_LOGE(TAG, "Previous run crashed (reset reason %d) but left no record", (int)reason);
    }
    // One crash is reported once, and a power-up never replays old RTC bytes.
    nb_panic_record_clear(&s_record);
}
