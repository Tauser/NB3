#include "clock_service.h"

#include <ctime>

#include <esp_log.h>
#include <nvs.h>

#define TAG "Clock"

namespace {

// No real time is earlier than this (2026-09-21, before this code existed).
constexpr int64_t kFloorS = 1790000000;
constexpr uint64_t kSamplePeriodUs = 1000 * 1000;
// The latest synced time is written down this often while it moves on.
constexpr uint32_t kPersistPeriodMs = 10 * 60 * 1000;
constexpr const char* kNamespace = "nb_clock";

uint64_t MonoMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

}  // namespace

ClockService::ClockService() {
    uint32_t boot_id = 0;
    int64_t last_seen = 0;
    nvs_handle_t nvs;
    if (nvs_open(kNamespace, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_get_u32(nvs, "boot_id", &boot_id);
        nvs_get_i64(nvs, "last_seen", &last_seen);
        boot_id++;
        nvs_set_u32(nvs, "boot_id", boot_id);
        nvs_commit(nvs);
        nvs_close(nvs);
    } else {
        ESP_LOGE(TAG, "NVS unavailable: boot id and last seen time will not persist");
    }
    nb_clock_init(&clock_, kFloorS, last_seen, boot_id);
    ESP_LOGI(TAG, "boot %lu, last synced time %lld", static_cast<unsigned long>(boot_id),
             static_cast<long long>(last_seen));
    Sample();

    esp_timer_create_args_t args = {};
    args.callback = OnTimer;
    args.arg = this;
    args.name = "nb_clock";
    if (esp_timer_create(&args, &timer_) != ESP_OK ||
        esp_timer_start_periodic(timer_, kSamplePeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "sample timer not running: the clock will never count as synced");
    }
}

ClockService::~ClockService() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void ClockService::OnTimer(void* arg) {
    static_cast<ClockService*>(arg)->Sample();
}

void ClockService::Sample() {
    std::lock_guard<std::mutex> lock(mutex_);
    const nb_clock_level_t before = clock_.level;
    const uint32_t syncs_before = clock_.syncs;
    const int64_t wall = static_cast<int64_t>(time(nullptr));
    if (!nb_clock_sample(&clock_, wall, MonoMs())) {
        Persist(false);
        return;
    }
    // Wall time is local time posing as UTC (xiaozhi adds the zone offset).
    const time_t t = static_cast<time_t>(wall);
    struct tm tm = {};
    gmtime_r(&t, &tm);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "%s -> %s (%s, syncs %lu)", nb_clock_level_name(before),
             nb_clock_level_name(clock_.level), text, static_cast<unsigned long>(clock_.syncs));
    Persist(clock_.syncs != syncs_before);
}

// Runs under mutex_.
void ClockService::Persist(bool force) {
    if (!clock_.last_seen_dirty) {
        return;
    }
    const uint32_t now = static_cast<uint32_t>(MonoMs());
    if (!force && now - last_persist_ms_ < kPersistPeriodMs) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(kNamespace, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (nvs_set_i64(nvs, "last_seen", clock_.last_seen_s) == ESP_OK && nvs_commit(nvs) == ESP_OK) {
        clock_.last_seen_dirty = false;
        last_persist_ms_ = now;
    }
    nvs_close(nvs);
}

nb_clock_level_t ClockService::Level() {
    std::lock_guard<std::mutex> lock(mutex_);
    return nb_clock_level(&clock_);
}

nb_stamp_t ClockService::Stamp() {
    std::lock_guard<std::mutex> lock(mutex_);
    return nb_clock_stamp(&clock_, MonoMs());
}

bool ClockService::Rebase(nb_stamp_t* stamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    return nb_clock_rebase(&clock_, stamp);
}

void ClockService::Snapshot(nb_clock_t* clock, uint64_t* now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    *clock = clock_;
    *now_ms = MonoMs();
}

bool ClockService::AgeSeconds(const nb_stamp_t& stamp, int64_t* age_s) {
    std::lock_guard<std::mutex> lock(mutex_);
    return nb_clock_age_s(&clock_, &stamp, MonoMs(), age_s);
}
