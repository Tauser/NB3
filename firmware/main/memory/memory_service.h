#ifndef NB3_MEMORY_SERVICE_H
#define NB3_MEMORY_SERVICE_H

#include <cstdint>
#include <functional>
#include <utility>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "behavior/mood_modifier.h"
#include "core/nb_memory.h"
#include "memory/clock_service.h"
#include "memory/store_service.h"

// The robot's medium and long memory (core/nb_memory, docs/VIDA.md §5). Its
// task is the only one that touches the memory: the rest report what
// happened and go on. It loads from the microSD when the card shows up,
// turns petting and conversations into episodes, keeps this boot's records
// dated once the clock syncs, feeds the mood bias from the recent episodes
// and compacts the log. Without a card there is no memory: events are
// dropped and the bias is zero.
class MemoryService {
public:
    MemoryService(StoreService& store, ClockService& clock, MoodModifier& mood);
    ~MemoryService();

    MemoryService(const MemoryService&) = delete;
    MemoryService& operator=(const MemoryService&) = delete;

    // From the store listener: the card came or went.
    void OnCard(bool available);
    void OnPetting(uint32_t duration_ms);
    // How the conversation left the mood (deviation from rest).
    void OnConversation(uint32_t duration_ms, nb_affect_vec_t feeling);

    // What a query sees: the memory, its io and the clock as of now.
    struct Access {
        nb_memory_t& memory;
        const nb_mem_io_t& io;
        const nb_clock_t& clock;
        uint64_t now_ms;
    };
    // Runs fn on the memory task and waits for it (the MCP tools). False,
    // with fn not run, while there is no memory: no card, or not loaded yet.
    bool Query(const std::function<void(const Access&)>& fn);

    // Called on the memory task with the habits whenever the bias is
    // refreshed (about once a minute), and with empty habits when the card
    // goes. Set once at boot, before the card can show up.
    using HabitsListener =
        std::function<void(const nb_mem_patterns_t& habits, int64_t now_s, bool now_known)>;
    void SetHabitsListener(HabitsListener fn) { habits_listener_ = std::move(fn); }

private:
    struct Message;
    struct Job;

    static void TaskEntry(void* arg);
    void Run();
    void Post(const Message& msg);
    void Handle(const Message& msg);
    void Load();
    void Tick();
    void AddEpisode(const nb_mem_episode_t& ep);
    void UpdateBias(const nb_clock_t& clock, uint64_t now_ms);

    static bool IoReadSlot(void* ctx, nb_mem_visit_t visit, void* arg, bool* found);
    static bool IoWriteSlot(void* ctx, const void* payload, uint32_t length, uint32_t max_seq);
    static bool IoAppend(void* ctx, uint32_t seq, const void* payload, uint32_t length);
    static bool IoReadLog(void* ctx, nb_mem_visit_t visit, void* arg);
    static bool IoClearLog(void* ctx);

    StoreService& store_;
    ClockService& clock_;
    MoodModifier& mood_;
    nb_mem_io_t io_;
    // Memory task only.
    nb_memory_t* memory_ = nullptr;
    bool loaded_ = false;
    uint32_t clock_syncs_ = 0;
    uint32_t last_bias_ms_ = 0;
    uint32_t last_flush_ms_ = 0;
    nb_affect_vec_t bias_ = {0.0f, 0.0f};
    HabitsListener habits_listener_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_MEMORY_SERVICE_H
