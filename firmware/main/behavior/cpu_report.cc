#include "cpu_report.h"

#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#define TAG "Cpu"

namespace {

constexpr uint32_t kPeriodMs = 30 * 1000;
constexpr int kMaxTasks = 80;
constexpr int kShown = 5;

struct Sample {
    TaskStatus_t tasks[kMaxTasks];
    UBaseType_t count = 0;
    uint32_t total = 0;
};

}  // namespace

CpuReport::CpuReport(FaceDisplay* display) : display_(display) {
    if (xTaskCreate(TaskEntry, "nb_cpu", 4096, this, 1, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task not created: no CPU report");
    }
}

CpuReport::~CpuReport() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
}

void CpuReport::TaskEntry(void* arg) {
    static_cast<CpuReport*>(arg)->Run();
}

void CpuReport::Run() {
    // Two samples of the task list, kept in PSRAM: the task stack is small.
    auto* before = static_cast<Sample*>(heap_caps_calloc(1, sizeof(Sample), MALLOC_CAP_SPIRAM));
    auto* now = static_cast<Sample*>(heap_caps_calloc(1, sizeof(Sample), MALLOC_CAP_SPIRAM));
    if (before == nullptr || now == nullptr) {
        ESP_LOGE(TAG, "no memory for the report");
        vTaskDelete(nullptr);
        return;
    }
    before->count = uxTaskGetSystemState(before->tasks, kMaxTasks, &before->total);
    int64_t last_us = esp_timer_get_time();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
        now->count = uxTaskGetSystemState(now->tasks, kMaxTasks, &now->total);
        const int64_t now_us = esp_timer_get_time();
        // The run-time counter ticks in microseconds on each core: the whole
        // machine is two cores' worth of that.
        const double window = static_cast<double>(now_us - last_us) * 2.0;
        last_us = now_us;

        double idle = 0.0;
        struct Busy {
            const char* name;
            double share;
        } top[kShown] = {};
        for (UBaseType_t i = 0; i < now->count; i++) {
            const TaskStatus_t& t = now->tasks[i];
            uint32_t was = 0;
            for (UBaseType_t j = 0; j < before->count; j++) {
                if (before->tasks[j].xHandle == t.xHandle) {
                    was = before->tasks[j].ulRunTimeCounter;
                    break;
                }
            }
            const double share = 100.0 * static_cast<double>(t.ulRunTimeCounter - was) / window;
            if (std::strncmp(t.pcTaskName, "IDLE", 4) == 0) {
                idle += share;
                continue;
            }
            for (int k = 0; k < kShown; k++) {
                if (share > top[k].share) {
                    for (int m = kShown - 1; m > k; m--) {
                        top[m] = top[m - 1];
                    }
                    top[k] = {t.pcTaskName, share};
                    break;
                }
            }
        }
        char busiest[160] = "";
        size_t used = 0;
        for (int k = 0; k < kShown && top[k].name != nullptr; k++) {
            used += std::snprintf(busiest + used, sizeof(busiest) - used, "%s%s %.1f%%",
                                  k > 0 ? ", " : "", top[k].name, top[k].share);
            if (used >= sizeof(busiest)) {
                break;
            }
        }
        FaceDisplay::DrawStats draw;
        if (display_ != nullptr) {
            draw = display_->TakeDrawStats();
        }
        ESP_LOGI(TAG, "idle %.0f%% of both cores; busiest: %s; face %.1f fps, eyes %lu us avg "
                 "%lu us max",
                 idle, busiest, draw.frames * 1000.0 / kPeriodMs,
                 static_cast<unsigned long>(draw.avg_us), static_cast<unsigned long>(draw.max_us));
        std::swap(before, now);
    }
}
