#ifndef NB3_CPU_REPORT_H
#define NB3_CPU_REPORT_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "face/face_display.h"

// A line in the log every half minute with where the CPU went (docs/ROADMAP.md,
// "measure CPU/FPS of the face"): the share of the two cores each of the
// busiest tasks used since the last line, how idle they were, and how the
// face drew (frames per second, time drawing the eyes). Run-time stats are
// already on in the build, so this costs one pass over the task list.
class CpuReport {
public:
    explicit CpuReport(FaceDisplay* display);
    ~CpuReport();

    CpuReport(const CpuReport&) = delete;
    CpuReport& operator=(const CpuReport&) = delete;

private:
    static void TaskEntry(void* arg);
    void Run();

    FaceDisplay* display_;
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_CPU_REPORT_H
