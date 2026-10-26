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
// interrupted write never corrupts what was there before. The card is never
// formatted by the firmware. Without a card, or after an I/O error, the
// service is unavailable (degraded): calls fail fast, the listener hears
// about it once, and mounting is retried in the background.
class StoreService {
public:
    StoreService();
    ~StoreService();

    StoreService(const StoreService&) = delete;
    StoreService& operator=(const StoreService&) = delete;

    bool Available() const { return available_; }
    // Called from the store task whenever the card comes or goes.
    void SetListener(std::function<void(bool available)> listener);

    // A small text file in the memory folder, e.g. the personality.
    bool ReadText(const char* name, std::string* out);

    // Loads the newest valid of the two slots of a record.
    bool ReadSlot(const char* record, std::vector<uint8_t>* payload, nb_slot_info_t* info);
    // Writes over the older or invalid slot; never the only valid one.
    bool WriteSlot(const char* record, const void* payload, uint32_t length, uint32_t max_seq);

    // Appends a frame, first dropping a torn tail left by a power loss.
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
    void Probe();

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    sdmmc_card_t* card_ = nullptr;
    volatile bool available_ = false;
    std::function<void(bool)> listener_;
};

#endif  // NB3_STORE_SERVICE_H
