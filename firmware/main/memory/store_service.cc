#include "store_service.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
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
// Each kind of file has its own folder, and the two slots of a record live
// in different ones: a torn directory sector damages entries of its own
// folder only, never personalidade.ini nor both copies of a record.
constexpr const char* kDir = "/sdcard/noise";  // personalidade.ini, read only
constexpr const char* kSlotDirs[2] = {"/sdcard/noise/slot_a", "/sdcard/noise/slot_b"};
constexpr const char* kLogDir = "/sdcard/noise/log";
constexpr const char* kProbeDir = "/sdcard/noise/teste";
// While the card is missing, mounting is retried this often.
constexpr TickType_t kRetryTicks = pdMS_TO_TICKS(30000);
// Fixed file sizes (core/nb_store): a slot fits the memory snapshot (96 KB of
// scratch) with room to spare; the log is compacted every 32 episodes of a
// few hundred bytes each.
constexpr long kSlotFileBytes = 128 * 1024;
constexpr long kLogFileBytes = 64 * 1024;

std::string PathOf(const char* name) {
    return std::string(kDir) + "/" + name;
}

std::string SlotPath(const char* record, int slot) {
    return std::string(kSlotDirs[slot]) + "/" + record + ".bin";
}

std::string LogPath(const char* log) {
    return std::string(kLogDir) + "/" + log;
}

std::string ProbePath(int slot) {
    return std::string(kProbeDir) + (slot == 0 ? "/probe_a.bin" : "/probe_b.bin");
}

bool EndsWith(const std::string& s, const char* suffix) {
    const size_t n = strlen(suffix);
    return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
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

bool ReadAt(const std::string& path, long offset, void* out, size_t size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    const bool ok = fseek(f, offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
    fclose(f);
    return ok;
}

// Overwrites bytes inside an existing file and forces them to the card
// before returning. The file keeps its size and clusters.
bool WriteAt(const std::string& path, long offset, const void* data, size_t size) {
    FILE* f = fopen(path.c_str(), "r+b");
    if (f == nullptr) {
        return false;
    }
    bool ok = fseek(f, offset, SEEK_SET) == 0 && (size == 0 || fwrite(data, 1, size, f) == size);
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    return fclose(f) == 0 && ok;
}

// Creates the file zero-filled at its final size when it is missing (or has
// another size, which only a foreign file would). This is the only place
// that allocates clusters.
bool EnsureFixedFile(const std::string& path, long size) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size == size) {
        return true;
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    static const uint8_t kZeros[1024] = {};
    bool ok = true;
    for (long done = 0; ok && done < size; done += sizeof(kZeros)) {
        ok = fwrite(kZeros, 1, sizeof(kZeros), f) == sizeof(kZeros);
    }
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    ESP_LOGI(TAG, "created %s (%ld KB)", path.c_str(), size / 1024);
    return ok;
}

// The slot at the start of a slot file, without its leftovers. False if
// the file is missing or holds no valid slot.
bool ReadSlotFile(const std::string& path, std::vector<uint8_t>* out, nb_slot_info_t* info,
                  const uint8_t** payload) {
    uint8_t header[NB_STORE_SLOT_HEADER_SIZE];
    if (!ReadAt(path, 0, header, sizeof(header))) {
        return false;
    }
    const size_t n = nb_slot_size(header, sizeof(header));
    if (n == 0 || n > static_cast<size_t>(kSlotFileBytes)) {
        return false;
    }
    out->resize(n);
    return ReadAt(path, 0, out->data(), n) && nb_slot_decode(out->data(), n, info, payload);
}

// Writes over the older or invalid of two slot files.
bool WriteSlotPair(const std::string paths[2], const void* payload, uint32_t length,
                   uint32_t max_seq, uint32_t* generation) {
    std::vector<uint8_t> files[2];
    nb_slot_info_t infos[2];
    bool valid[2];
    for (int s = 0; s < 2; s++) {
        valid[s] = ReadSlotFile(paths[s], &files[s], &infos[s], nullptr);
    }
    const nb_slot_choice_t choice = nb_slot_choose(valid[0], &infos[0], valid[1], &infos[1]);
    std::vector<uint8_t> out(NB_STORE_SLOT_HEADER_SIZE + length);
    const size_t n =
        nb_slot_encode(choice.next_gen, max_seq, payload, length, out.data(), out.size());
    if (n == 0) {
        return false;
    }
    if (generation != nullptr) {
        *generation = choice.next_gen;
    }
    return EnsureFixedFile(paths[choice.write], kSlotFileBytes) &&
           WriteAt(paths[choice.write], 0, out.data(), n);
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
            if (!available_) {
                if (Mount()) {
                    Probe();
                } else if (listener_) {
                    listener_(false);  // still missing: the listener decides whether to remind
                }
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
    for (const char* dir : {kDir, kSlotDirs[0], kSlotDirs[1], kLogDir, kProbeDir}) {
        if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
            Fail("mkdir");
            return false;
        }
    }
    if (!MoveOldFiles()) {
        Fail("moving old files");
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

// Store task only: files of the earlier layout, all in noise/ and rewritten
// whole on every save, move into their folders as fixed-size files. Each
// old file is removed only after its copy is on the card, so a cut in the
// middle repeats the move on the next mount.
bool StoreService::MoveOldFiles() {
    std::vector<std::string> names;
    DIR* dir = opendir(kDir);
    if (dir == nullptr) {
        return false;
    }
    while (dirent* e = readdir(dir)) {
        if (e->d_type == DT_REG) {
            names.emplace_back(e->d_name);
        }
    }
    closedir(dir);
    for (const std::string& name : names) {
        const std::string old_path = PathOf(name.c_str());
        std::string new_path;
        long size = 0;
        std::vector<uint8_t> bytes;
        if (EndsWith(name, "_a.bin") || EndsWith(name, "_b.bin")) {
            const std::string record = name.substr(0, name.size() - 6);
            if (record != "probe") {
                new_path = SlotPath(record.c_str(), name[name.size() - 5] == 'a' ? 0 : 1);
                size = kSlotFileBytes;
                if (!ReadFile(old_path, &bytes, kSlotFileBytes)) {
                    ESP_LOGE(TAG, "old %s not readable; left in place", name.c_str());
                    continue;
                }
            }
        } else if (EndsWith(name, ".log")) {
            new_path = LogPath(name.c_str());
            size = kLogFileBytes;
            if (!ReadFile(old_path, &bytes, kLogFileBytes - NB_STORE_LOG_END)) {
                ESP_LOGE(TAG, "old %s not readable; left in place", name.c_str());
                continue;
            }
            // Its valid frames, then the end marker.
            bytes.resize(nb_log_scan(bytes.data(), bytes.size(), nullptr, nullptr).valid_bytes);
            bytes.resize(bytes.size() + NB_STORE_LOG_END, 0);
        } else {
            continue;  // personalidade.ini and anything the user put there
        }
        if (!new_path.empty() &&
            (!EnsureFixedFile(new_path, size) ||
             !WriteAt(new_path, 0, bytes.data(), bytes.size()))) {
            return false;
        }
        if (remove(old_path.c_str()) != 0) {
            return false;
        }
        ESP_LOGI(TAG, "moved %s to %s", name.c_str(),
                 new_path.empty() ? "(dropped)" : new_path.c_str());
    }
    return true;
}

// Store task only: one small slot write and read-back per mount, so the log
// says whether the card really keeps what it is given.
void StoreService::Probe() {
    const std::string paths[2] = {ProbePath(0), ProbePath(1)};
    static const char kText[] = "noise";
    uint32_t generation = 0;
    std::vector<uint8_t> back[2];
    nb_slot_info_t got[2];
    bool valid[2];
    if (!WriteSlotPair(paths, kText, sizeof(kText), 0, &generation)) {
        Fail("probe");
        return;
    }
    for (int s = 0; s < 2; s++) {
        valid[s] = ReadSlotFile(paths[s], &back[s], &got[s], nullptr);
    }
    const nb_slot_choice_t now = nb_slot_choose(valid[0], &got[0], valid[1], &got[1]);
    if (now.load < 0 || got[now.load].generation != generation) {
        Fail("probe");
        return;
    }
    ESP_LOGI(TAG, "probe ok: slot %c, generation %lu", now.load == 0 ? 'A' : 'B',
             static_cast<unsigned long>(generation));
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
        for (int s = 0; s < 2; s++) {
            valid[s] = ReadSlotFile(SlotPath(record, s), &files[s], &infos[s], &data[s]);
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
        const std::string paths[2] = {SlotPath(record, 0), SlotPath(record, 1)};
        if (NB_STORE_SLOT_HEADER_SIZE + static_cast<long>(length) > kSlotFileBytes) {
            ESP_LOGE(TAG, "%s: %lu bytes do not fit a slot", record,
                     static_cast<unsigned long>(length));
            return false;
        }
        if (!WriteSlotPair(paths, payload, length, max_seq, nullptr)) {
            Fail("slot write");
            return false;
        }
        return true;
    });
}

bool StoreService::AppendFrame(const char* log, uint32_t seq, const void* payload,
                               uint32_t length) {
    return Do([this, log, seq, payload, length]() {
        const std::string path = LogPath(log);
        std::vector<uint8_t> bytes;
        if (!EnsureFixedFile(path, kLogFileBytes) || !ReadFile(path, &bytes, kLogFileBytes)) {
            Fail("log open");
            return false;
        }
        // The entry goes over a torn tail or the end marker of the last frame.
        const size_t at = nb_log_scan(bytes.data(), bytes.size(), nullptr, nullptr).valid_bytes;
        std::vector<uint8_t> entry(nb_log_entry_size(length));
        if (nb_log_entry_encode(seq, payload, length, entry.data(), entry.size()) == 0) {
            return false;
        }
        if (at + entry.size() > bytes.size()) {
            ESP_LOGE(TAG, "%s full; frame %lu not saved", log, static_cast<unsigned long>(seq));
            return false;
        }
        if (!WriteAt(path, static_cast<long>(at), entry.data(), entry.size())) {
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
        if (ReadFile(LogPath(log), &bytes, kLogFileBytes)) {
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
        static const uint8_t kEnd[NB_STORE_LOG_END] = {};
        const std::string path = LogPath(log);
        if (!EnsureFixedFile(path, kLogFileBytes) || !WriteAt(path, 0, kEnd, sizeof(kEnd))) {
            Fail("log clear");
            return false;
        }
        return true;
    });
}
