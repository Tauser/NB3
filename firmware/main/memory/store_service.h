#ifndef NB3_STORE_SERVICE_H
#define NB3_STORE_SERVICE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

#include "core/nb_store.h"

// The microSD (docs/VIDA.md §5.2). Its task is the only owner of the card:
// every other task asks and waits for the answer. Long records live in two
// CRC-checked slots and episodes in a framed log (core/nb_store), so an
// interrupted write never corrupts what was there before. Those files have
// a fixed size and their own folders, so saving does not rewrite the FAT
// and a torn directory sector cannot reach personalidade.ini. The card is never
// formatted by the firmware. Without a card, or after an I/O error, the
// service is unavailable (degraded): calls fail fast, the listener hears
// about it, and mounting is retried in the background.
class StoreService {
public:
    StoreService();
    ~StoreService();

    StoreService(const StoreService&) = delete;
    StoreService& operator=(const StoreService&) = delete;

    bool Available() const { return available_; }
    // Called from the store task when the card comes or goes, and again after
    // every failed retry while it is missing.
    void SetListener(std::function<void(bool available)> listener);

    // A small text file in the memory folder, e.g. the personality.
    bool ReadText(const char* name, std::string* out);
    // A file in the memory folder ("sons/lembrete.ogg"), refused above
    // max_bytes. Not from the store task itself (the listener).
    bool ReadBytes(const char* name, std::vector<uint8_t>* out, long max_bytes);

    struct Entry {
        std::string name;
        long size;
        bool is_dir;
    };
    // The entries of a folder of the memory folder ("" is the folder itself).
    bool ListDir(const char* dir, std::vector<Entry>* out);
    // Writes a file in the memory folder through a temporary one and a rename,
    // so a failure leaves the old file or none, never half of a new one.
    bool WriteFile(const char* name, const void* data, size_t length);
    bool DeleteFile(const char* name);
    // Bytes used and total on the card, for the stats.
    bool Capacity(uint64_t* total_bytes, uint64_t* free_bytes);

    // Loads the newest valid of the two slots of a record.
    bool ReadSlot(const char* record, std::vector<uint8_t>* payload, nb_slot_info_t* info);
    // Writes over the older or invalid slot; never the only valid one.
    bool WriteSlot(const char* record, const void* payload, uint32_t length, uint32_t max_seq);

    // Appends a frame over a torn tail left by a power loss, if any. False
    // without touching the card when the log is full.
    bool AppendFrame(const char* log, uint32_t seq, const void* payload, uint32_t length);
    // The log's valid frames, in order.
    bool ReadLog(const char* log, nb_frame_visit_t visit, void* ctx, nb_log_scan_t* scan);
    bool ClearLog(const char* log);

private:
    struct Job;

    static void TaskEntry(void* arg);
    void Run();
    bool Do(std::function<bool()> work);
    bool Mount();
    void Fail(const char* what);
    bool MoveOldFiles();
    void Probe();

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    sdmmc_card_t* card_ = nullptr;
    volatile bool available_ = false;
    std::function<void(bool)> listener_;
};

#endif  // NB3_STORE_SERVICE_H
