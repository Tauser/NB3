#include "store_service.h"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <freertos/semphr.h>

#include "board/config.h"

#define TAG "Store"

namespace {

constexpr const char* kMountPoint = "/sdcard";
constexpr const char* kDir = "/sdcard/noise";
// While the card is missing, mounting is retried this often.
constexpr TickType_t kRetryTicks = pdMS_TO_TICKS(30000);
// Logs are compacted into slots (VIDA.md §5.2), so they stay small; a log
// larger than this is not read whole.
constexpr long kMaxLogBytes = 1024 * 1024;

std::string PathOf(const char* name) {
    return std::string(kDir) + "/" + name;
}

std::string SlotPath(const char* record, int slot) {
    return PathOf(record) + (slot == 0 ? "_a.bin" : "_b.bin");
}

// Reads a whole file. False if it cannot be opened (missing is normal) or
// is larger than max_bytes.
bool ReadFile(const std::string& path, std::vector<uint8_t>* out, long max_bytes) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    bool ok = fseek(f, 0, SEEK_END) == 0;
    const long size = ok ? ftell(f) : -1;
    ok = ok && size >= 0 && size <= max_bytes && fseek(f, 0, SEEK_SET) == 0;
    if (ok) {
        out->resize(static_cast<size_t>(size));
        ok = size == 0 || fread(out->data(), 1, out->size(), f) == out->size();
    }
    fclose(f);
    return ok;
}

// Writes and forces the bytes to the card before returning.
bool WriteFileSynced(const std::string& path, const char* mode, const void* data, size_t size) {
    FILE* f = fopen(path.c_str(), mode);
    if (f == nullptr) {
        return false;
    }
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    return fclose(f) == 0 && ok;
}

}  // namespace

struct StoreService::Job {
    std::function<bool()> work;
    bool needs_card;
    SemaphoreHandle_t done;
    bool result;
};

StoreService::StoreService() {
    queue_ = xQueueCreate(8, sizeof(Job*));
    if (queue_ == nullptr ||
        xTaskCreate(TaskEntry, "nb_store", 6144, this, 3, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "store task not created: memory unavailable");
    }
}

StoreService::~StoreService() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    if (card_ != nullptr) {
        esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
    }
}

void StoreService::TaskEntry(void* arg) {
    static_cast<StoreService*>(arg)->Run();
}

void StoreService::Run() {
    if (Mount()) {
        Probe();
    }
    while (true) {
        Job* job = nullptr;
        if (xQueueReceive(queue_, &job, available_ ? portMAX_DELAY : kRetryTicks) != pdTRUE) {
            if (!available_ && Mount()) {
                Probe();
            }
            continue;
        }
        job->result = (!job->needs_card || available_) ? job->work() : false;
        xSemaphoreGive(job->done);
    }
}

// Runs work on the store task and waits for it.
bool StoreService::Do(std::function<bool()> work) {
    if (task_ == nullptr) {
        return false;
    }
    Job job{std::move(work), true, xSemaphoreCreateBinary(), false};
    if (job.done == nullptr) {
        return false;
    }
    Job* ptr = &job;
    xQueueSend(queue_, &ptr, portMAX_DELAY);
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return job.result;
}

void StoreService::SetListener(std::function<void(bool)> listener) {
    if (task_ == nullptr) {
        if (listener) {
            listener(false);
        }
        return;
    }
    Job job{[this, &listener]() {
                listener_ = std::move(listener);
                if (listener_) {
                    listener_(available_);
                }
                return true;
            },
            false, xSemaphoreCreateBinary(), false};
    if (job.done == nullptr) {
        return;
    }
    Job* ptr = &job;
    xQueueSend(queue_, &ptr, portMAX_DELAY);
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
}

// Store task only.
bool StoreService::Mount() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK_PIN;
    slot.cmd = SD_CMD_PIN;
    slot.d0 = SD_D0_PIN;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;  // never erase someone's memory
    mount.max_files = 4;
    mount.allocation_unit_size = 16 * 1024;

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &mount, &card_);
    if (err != ESP_OK) {
        card_ = nullptr;
        ESP_LOGW(TAG, "no card (%s); memory unavailable, retrying every %lu s",
                 esp_err_to_name(err), static_cast<unsigned long>(pdTICKS_TO_MS(kRetryTicks) / 1000));
        return false;
    }
    if (mkdir(kDir, 0775) != 0 && errno != EEXIST) {
        Fail("mkdir");
        return false;
    }
    const uint64_t mb = static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size /
                        (1024 * 1024);
    ESP_LOGI(TAG, "card %s, %llu MB, mounted", card_->cid.name,
             static_cast<unsigned long long>(mb));
    available_ = true;
    if (listener_) {
        listener_(true);
    }
    return true;
}

// Store task only: an I/O error means the card cannot be trusted now.
void StoreService::Fail(const char* what) {
    ESP_LOGE(TAG, "%s failed (errno %d); memory unavailable until the card is back", what, errno);
    if (card_ != nullptr) {
        esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
        card_ = nullptr;
    }
    const bool was = available_;
    available_ = false;
    if (was && listener_) {
        listener_(false);
    }
}

// Store task only: one small slot write and read-back per mount, so the log
// says whether the card really keeps what it is given.
void StoreService::Probe() {
    std::vector<uint8_t> files[2];
    nb_slot_info_t info[2];
    bool valid[2];
    for (int s = 0; s < 2; s++) {
        valid[s] = ReadFile(SlotPath("probe", s), &files[s], 1024) &&
                   nb_slot_decode(files[s].data(), files[s].size(), &info[s], nullptr);
    }
    const nb_slot_choice_t choice = nb_slot_choose(valid[0], &info[0], valid[1], &info[1]);
    static const char kText[] = "noise";
    uint8_t buf[NB_STORE_SLOT_HEADER_SIZE + sizeof(kText)];
    const size_t n = nb_slot_encode(choice.next_gen, 0, kText, sizeof(kText), buf, sizeof(buf));
    std::vector<uint8_t> back;
    nb_slot_info_t got;
    if (!WriteFileSynced(SlotPath("probe", choice.write), "wb", buf, n) ||
        !ReadFile(SlotPath("probe", choice.write), &back, 1024) ||
        !nb_slot_decode(back.data(), back.size(), &got, nullptr) ||
        got.generation != choice.next_gen) {
        Fail("probe");
        return;
    }
    ESP_LOGI(TAG, "probe ok: slot %c, generation %lu", choice.write == 0 ? 'A' : 'B',
             static_cast<unsigned long>(got.generation));
}

bool StoreService::ReadText(const char* name, std::string* out) {
    return Do([name, out]() {
        std::vector<uint8_t> bytes;
        if (!ReadFile(PathOf(name), &bytes, 16 * 1024)) {
            return false;
        }
        out->assign(bytes.begin(), bytes.end());
        return true;
    });
}

bool StoreService::ReadSlot(const char* record, std::vector<uint8_t>* payload,
                            nb_slot_info_t* info) {
    return Do([record, payload, info]() {
        std::vector<uint8_t> files[2];
        nb_slot_info_t infos[2];
        const uint8_t* data[2] = {nullptr, nullptr};
        bool valid[2];
        const long max = NB_STORE_SLOT_HEADER_SIZE + NB_STORE_MAX_PAYLOAD;
        for (int s = 0; s < 2; s++) {
            valid[s] = ReadFile(SlotPath(record, s), &files[s], max) &&
                       nb_slot_decode(files[s].data(), files[s].size(), &infos[s], &data[s]);
        }
        const nb_slot_choice_t choice = nb_slot_choose(valid[0], &infos[0], valid[1], &infos[1]);
        if (choice.load < 0) {
            return false;
        }
        payload->assign(data[choice.load], data[choice.load] + infos[choice.load].length);
        if (info != nullptr) {
            *info = infos[choice.load];
        }
        return true;
    });
}

bool StoreService::WriteSlot(const char* record, const void* payload, uint32_t length,
                             uint32_t max_seq) {
    return Do([this, record, payload, length, max_seq]() {
        std::vector<uint8_t> files[2];
        nb_slot_info_t infos[2];
        bool valid[2];
        const long max = NB_STORE_SLOT_HEADER_SIZE + NB_STORE_MAX_PAYLOAD;
        for (int s = 0; s < 2; s++) {
            valid[s] = ReadFile(SlotPath(record, s), &files[s], max) &&
                       nb_slot_decode(files[s].data(), files[s].size(), &infos[s], nullptr);
        }
        const nb_slot_choice_t choice = nb_slot_choose(valid[0], &infos[0], valid[1], &infos[1]);
        std::vector<uint8_t> out(NB_STORE_SLOT_HEADER_SIZE + length);
        const size_t n =
            nb_slot_encode(choice.next_gen, max_seq, payload, length, out.data(), out.size());
        if (n == 0) {
            return false;
        }
        if (!WriteFileSynced(SlotPath(record, choice.write), "wb", out.data(), n)) {
            Fail("slot write");
            return false;
        }
        return true;
    });
}

bool StoreService::AppendFrame(const char* log, uint32_t seq, const void* payload,
                               uint32_t length) {
    return Do([this, log, seq, payload, length]() {
        const std::string path = PathOf(log);
        std::vector<uint8_t> bytes;
        if (ReadFile(path, &bytes, kMaxLogBytes)) {
            const nb_log_scan_t scan = nb_log_scan(bytes.data(), bytes.size(), nullptr, nullptr);
            if (scan.valid_bytes < bytes.size() &&
                truncate(path.c_str(), static_cast<off_t>(scan.valid_bytes)) != 0) {
                Fail("log truncate");
                return false;
            }
        }
        std::vector<uint8_t> frame(nb_frame_size(length));
        if (nb_frame_encode(seq, payload, length, frame.data(), frame.size()) == 0) {
            return false;
        }
        if (!WriteFileSynced(path, "ab", frame.data(), frame.size())) {
            Fail("log append");
            return false;
        }
        return true;
    });
}

bool StoreService::ReadLog(const char* log, nb_frame_visit_t visit, void* ctx,
                           nb_log_scan_t* scan) {
    return Do([log, visit, ctx, scan]() {
        std::vector<uint8_t> bytes;
        nb_log_scan_t result = {0, 0, 0};
        if (ReadFile(PathOf(log), &bytes, kMaxLogBytes)) {
            result = nb_log_scan(bytes.data(), bytes.size(), visit, ctx);
        }
        if (scan != nullptr) {
            *scan = result;
        }
        return true;  // a missing log is an empty one
    });
}

bool StoreService::ClearLog(const char* log) {
    return Do([this, log]() {
        if (!WriteFileSynced(PathOf(log), "wb", nullptr, 0)) {
            Fail("log clear");
            return false;
        }
        return true;
    });
}
