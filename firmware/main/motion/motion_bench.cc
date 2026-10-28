#include "motion_bench.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MotionBench"

#ifdef NB3_MOTION_BENCH
namespace {

struct Step {
    uint16_t pan, tilt;
};

// Inside the default limits (pan 410..614, tilt 461..563), well short of them.
constexpr Step kSteps[] = {
    {512, 512}, {460, 512}, {564, 512}, {512, 480}, {512, 544}, {512, 512},
};
constexpr uint16_t kMoveMs = 800;
constexpr uint32_t kStepGapMs = 2000;
constexpr uint32_t kHoldMs = 20000;

void Run(void* arg) {
    auto& motion = *static_cast<MotionService*>(arg);
    vTaskDelay(pdMS_TO_TICKS(10000));
    for (int round = 1;; round++) {
        if (motion.State() == NB_MOTION_SAFETY_STATE_FAULT) {
            ESP_LOGW(TAG, "stopped: %s; waiting for self.head.recover",
                     nb_motion_safety_fault_name(motion.Fault()));
            while (motion.State() == NB_MOTION_SAFETY_STATE_FAULT) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            ESP_LOGI(TAG, "recovered: going on");
        }
        ESP_LOGI(TAG, "round %d", round);
        for (const Step& s : kSteps) {
            if (motion.State() == NB_MOTION_SAFETY_STATE_FAULT) {
                break;
            }
            ESP_LOGI(TAG, "pan %u, tilt %u", s.pan, s.tilt);
            nb_head_plan_t plan = {};
            plan.points[0] = {s.pan, s.tilt, kMoveMs};
            plan.count = 1;
            plan.total_ms = kMoveMs;
            motion.Play(plan);
            vTaskDelay(pdMS_TO_TICKS(kStepGapMs));
        }
        ESP_LOGI(TAG, "holding center for %lu s: try holding a horn",
                 static_cast<unsigned long>(kHoldMs / 1000));
        vTaskDelay(pdMS_TO_TICKS(kHoldMs));
    }
}

}  // namespace
#endif

void StartMotionBench(MotionService& motion) {
#ifdef NB3_MOTION_BENCH
    xTaskCreate(Run, "nb_motion_bench", 3072, &motion, 2, nullptr);
#else
    (void)motion;
#endif
}
