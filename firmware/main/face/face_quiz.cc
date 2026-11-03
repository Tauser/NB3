#include "face_quiz.h"

#include <cstdio>

#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "FaceQuiz"

namespace {

// Every face the arbiter can resolve on its own. The listening bars, the
// thinking hand and the speech bubble depend on the device state, so they are
// not part of this test.
constexpr nb_expression_t kFaces[] = {
    NB_EXPR_NEUTRAL,   NB_EXPR_HAPPY,    NB_EXPR_EXCITED,    NB_EXPR_CURIOUS,
    NB_EXPR_CONFUSED,  NB_EXPR_ANGRY,    NB_EXPR_SURPRISED,  NB_EXPR_SAD,
    NB_EXPR_SLEEPY,    NB_EXPR_DIZZY,    NB_EXPR_LOVING,     NB_EXPR_EMBARRASSED,
    NB_EXPR_CONTENT,
};
constexpr int kFaceCount = sizeof(kFaces) / sizeof(kFaces[0]);
constexpr int kPasses = 3;
constexpr uint32_t kStartDelayMs = 20000;  // the boot settles first
constexpr uint32_t kGapMs = 3000;          // the number of the item, alone on the screen
constexpr uint32_t kShowMs = 5000;         // the face, with nothing else

}  // namespace

FaceQuiz::FaceQuiz(ExpressionArbiter& arbiter, FaceDisplay* display)
    : arbiter_(arbiter), display_(display) {}

void FaceQuiz::Start() {
#ifdef NB3_FACE_QUIZ
    if (xTaskCreate(TaskEntry, "nb_quiz", 4096, this, 2, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "task not created: no reading test");
    }
#endif
}

void FaceQuiz::TaskEntry(void* arg) {
    static_cast<FaceQuiz*>(arg)->Run();
    vTaskDelete(nullptr);
}

void FaceQuiz::Run() {
    vTaskDelay(pdMS_TO_TICKS(kStartDelayMs));
    ESP_LOGI(TAG, "reading test: %d passes of %d faces, key follows", kPasses, kFaceCount);
    int item = 0;
    for (int pass = 1; pass <= kPasses; pass++) {
        int order[kFaceCount];
        for (int i = 0; i < kFaceCount; i++) {
            order[i] = i;
        }
        for (int i = kFaceCount - 1; i > 0; i--) {  // Fisher-Yates
            const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
            const int swap = order[i];
            order[i] = order[j];
            order[j] = swap;
        }
        for (int k = 0; k < kFaceCount; k++) {
            item++;
            char number[16];
            std::snprintf(number, sizeof(number), "%d", item);
            display_->ShowNotification(number, static_cast<int>(kGapMs));
            vTaskDelay(pdMS_TO_TICKS(kGapMs));
            const nb_expression_t face = kFaces[order[k]];
            const nb_proposal_t proposal = {face, priority::kCommand, 0, false};
            arbiter_.Propose(NB_SOURCE_COMMAND, proposal);
            ESP_LOGI(TAG, "KEY item %d pass %d face %s", item, pass, nb_expression_name(face));
            vTaskDelay(pdMS_TO_TICKS(kShowMs));
            arbiter_.Withdraw(NB_SOURCE_COMMAND);
        }
    }
    ESP_LOGI(TAG, "reading test over: %d items", item);
}
