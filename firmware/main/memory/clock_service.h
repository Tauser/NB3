#ifndef NB3_CLOCK_SERVICE_H
#define NB3_CLOCK_SERVICE_H

#include <cstdint>
#include <mutex>

#include <esp_timer.h>

#include "core/nb_clock.h"

// Wall-clock trust for the memory (core/nb_clock, docs/VIDA.md §5.1). Samples
// the system clock once a second to catch xiaozhi's sync, keeps a boot
// counter and the latest synced time in NVS, and stamps records so their age
// stays right when the time arrives or is corrected. The microSD memory will
// use it; today it only reports what it knows.
class ClockService {
public:
    ClockService();
    ~ClockService();

    ClockService(const ClockService&) = delete;
    ClockService& operator=(const ClockService&) = delete;

    nb_clock_level_t Level();
    nb_stamp_t Stamp();
    // Rebases a stamp from this boot after a sync or a correction.
    bool Rebase(nb_stamp_t* stamp);
    // False when the age is unknown (no synced time on either end).
    bool AgeSeconds(const nb_stamp_t& stamp, int64_t* age_s);

private:
    static void OnTimer(void* arg);
    void Sample();
    void Persist(bool force);

    std::mutex mutex_;
    nb_clock_t clock_;
    uint32_t last_persist_ms_ = 0;
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_CLOCK_SERVICE_H
