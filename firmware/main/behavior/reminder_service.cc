#include "reminder_service.h"

#include <cstdio>
#include <string>

#include <esp_log.h>
#include <nvs.h>

#include "application.h"
#include "assets/lang_config.h"

#define TAG "Reminder"

namespace {

constexpr const char* kNamespace = "nb_remind";
constexpr const char* kKey = "list";
constexpr uint32_t kPeriodMs = 500;
constexpr uint32_t kFaceMs = 4000;
constexpr int kBannerMs = 15000;

std::string LateText(int64_t late_s) {
    if (late_s < 60) {
        return "";
    }
    char text[80];
    if (late_s < 3600) {
        snprintf(text, sizeof(text), " (atrasado %lld min)", static_cast<long long>(late_s / 60));
    } else {
        snprintf(text, sizeof(text), " (atrasado %lld h, o robô estava desligado)",
                 static_cast<long long>(late_s / 3600));
    }
    return text;
}

}  // namespace

ReminderService::ReminderService(ClockService& clock, ExpressionArbiter& arbiter,
                                 FaceDisplay* display, HeadBehavior& head)
    : clock_(clock), arbiter_(arbiter), display_(display), head_(head) {
    nb_reminders_init(&reminders_);
    Load();
    // Room for the fired list and the encoded blob on the stack.
    if (xTaskCreate(TaskEntry, "nb_remind", 6144, this, 3, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task not created: reminders will not ring");
    }
}

ReminderService::~ReminderService() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
}

void ReminderService::TaskEntry(void* arg) {
    static_cast<ReminderService*>(arg)->Run();
}

void ReminderService::Load() {
    nvs_handle_t nvs;
    if (nvs_open(kNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return;  // none saved yet
    }
    uint8_t blob[NB_REMINDER_BLOB_MAX];
    size_t len = sizeof(blob);
    if (nvs_get_blob(nvs, kKey, blob, &len) == ESP_OK) {
        const int dropped = nb_reminders_decode(&reminders_, blob, len);
        ESP_LOGI(TAG, "loaded %d reminder(s), %d dropped (set before the clock was known)",
                 nb_reminders_count(&reminders_), dropped);
    }
    nvs_close(nvs);
}

// Under mutex_.
void ReminderService::Save() {
    if (!reminders_.dirty) {
        return;
    }
    uint8_t blob[NB_REMINDER_BLOB_MAX];
    const size_t len = nb_reminders_encode(&reminders_, blob, sizeof(blob));
    nvs_handle_t nvs;
    if (len == 0 || nvs_open(kNamespace, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "not saved: they live until the next restart");
        return;
    }
    if (nvs_set_blob(nvs, kKey, blob, len) == ESP_OK && nvs_commit(nvs) == ESP_OK) {
        reminders_.dirty = false;
    }
    nvs_close(nvs);
}

void ReminderService::Use(const std::function<void(const Access&)>& fn) {
    nb_clock_t clock;
    uint64_t now = 0;
    clock_.Snapshot(&clock, &now);
    std::lock_guard<std::mutex> lock(mutex_);
    fn({reminders_, clock, now});
    Save();
}

void ReminderService::Ring(const nb_reminder_fired_t& fired) {
    const std::string late = LateText(fired.late_s);
    ESP_LOGI(TAG, "ringing %lu: \"%s\"%s", static_cast<unsigned long>(fired.id), fired.text,
             late.c_str());
    nb_proposal_t face = {NB_EXPR_SURPRISED, priority::kReflex, kFaceMs, false};
    arbiter_.Propose(NB_SOURCE_REFLEX, face);
    head_.OnAlert();
    if (display_ != nullptr) {
        display_->ShowNotification(std::string("Lembrete: ") + fired.text, kBannerMs);
    }
    auto& app = Application::GetInstance();
    app.PlaySound(Lang::Sounds::OGG_POPUP);
    // Online and quiet: the AI says it. Otherwise the sound and the banner
    // are the reminder.
    if (app.GetDeviceState() == kDeviceStateIdle) {
        app.WakeWordInvoke(std::string("Lembrete agora: ") + fired.text + late);
    }
}

void ReminderService::Run() {
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        nb_reminder_fired_t fired[NB_REMINDER_MAX];
        int n = 0;
        nb_clock_t clock;
        uint64_t now = 0;
        clock_.Snapshot(&clock, &now);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            n = nb_reminders_update(&reminders_, &clock, now, fired, NB_REMINDER_MAX);
            Save();
        }
        for (int i = 0; i < n; i++) {
            Ring(fired[i]);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
    }
}
