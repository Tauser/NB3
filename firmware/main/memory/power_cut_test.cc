#include "power_cut_test.h"

#include <cstdio>
#include <string>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include "core/nb_memory.h"

#define TAG "PowerCut"

namespace {

constexpr const char* kRecord = "cortes";      // never the robot's memory files
constexpr const char* kLog = "cortes.log";
constexpr const char* kNamespace = "nb_cut";
constexpr uint32_t kLongLimit = 40;  // forget the oldest fact above this

struct Counters {
    uint32_t boots = 0;
    uint32_t failures = 0;
    uint32_t last_kept = 0;    // id of the last fact whose save was confirmed
    uint32_t last_forgot = 0;  // id of the last fact whose forget was confirmed
    uint32_t ini_crc = 0;      // personalidade.ini as first seen; 0 = not seen yet
};

void LoadCounters(Counters* c) {
    nvs_handle_t nvs;
    if (nvs_open(kNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    nvs_get_u32(nvs, "boots", &c->boots);
    nvs_get_u32(nvs, "failures", &c->failures);
    nvs_get_u32(nvs, "kept", &c->last_kept);
    nvs_get_u32(nvs, "forgot", &c->last_forgot);
    nvs_get_u32(nvs, "ini_crc", &c->ini_crc);
    nvs_close(nvs);
}

// Written only after the card confirmed: a cut between the two leaves the
// counters behind the card, which the checks tolerate.
void SaveCounters(const Counters& c) {
    nvs_handle_t nvs;
    if (nvs_open(kNamespace, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_u32(nvs, "boots", c.boots);
    nvs_set_u32(nvs, "failures", c.failures);
    nvs_set_u32(nvs, "kept", c.last_kept);
    nvs_set_u32(nvs, "forgot", c.last_forgot);
    nvs_set_u32(nvs, "ini_crc", c.ini_crc);
    nvs_commit(nvs);
    nvs_close(nvs);
}

bool ReadSlot(void* ctx, nb_mem_visit_t visit, void* arg, bool* found) {
    auto* store = static_cast<StoreService*>(ctx);
    std::vector<uint8_t> payload;
    nb_slot_info_t info;
    if (!store->ReadSlot(kRecord, &payload, &info)) {
        *found = false;
        return store->Available();
    }
    *found = true;
    visit(payload.data(), static_cast<uint32_t>(payload.size()), info.max_seq, arg);
    return true;
}

bool WriteSlot(void* ctx, const void* payload, uint32_t length, uint32_t max_seq) {
    return static_cast<StoreService*>(ctx)->WriteSlot(kRecord, payload, length, max_seq);
}

bool Append(void* ctx, uint32_t seq, const void* payload, uint32_t length) {
    return static_cast<StoreService*>(ctx)->AppendFrame(kLog, seq, payload, length);
}

struct LogVisit {
    nb_mem_visit_t visit;
    void* arg;
};

bool VisitFrame(uint32_t seq, const uint8_t* payload, uint32_t length, void* ctx) {
    auto* v = static_cast<LogVisit*>(ctx);
    return v->visit(payload, length, seq, v->arg);
}

bool ReadLog(void* ctx, nb_mem_visit_t visit, void* arg) {
    LogVisit v = {visit, arg};
    return static_cast<StoreService*>(ctx)->ReadLog(kLog, VisitFrame, &v, nullptr);
}

bool ClearLog(void* ctx) {
    return static_cast<StoreService*>(ctx)->ClearLog(kLog);
}

// The firmware never writes personalidade.ini, yet cuts in the old layout
// damaged it: it must read the same as the first time this test saw it.
std::string CheckPersonality(StoreService& store, Counters* c) {
    std::string text;
    if (!store.ReadText("personalidade.ini", &text)) {
        return c->ini_crc == 0 ? "" : "personalidade.ini gone";
    }
    const uint32_t crc = nb_store_crc32(0, text.data(), text.size()) | 1u;  // never 0
    if (c->ini_crc == 0) {
        c->ini_crc = crc;
        ESP_LOGI(TAG, "personalidade.ini watched from now on (%u bytes)",
                 static_cast<unsigned>(text.size()));
        return "";
    }
    return crc == c->ini_crc ? "" : "personalidade.ini changed";
}

// What a boot must find after any cut. Returns the first problem, or "".
std::string Check(const nb_memory_t& m, nb_mem_status_t load, const Counters& c) {
    if (load != NB_MEM_OK) {
        return load == NB_MEM_IO ? "load: card error" : "load: snapshot unreadable";
    }
    if (m.rejected != 0) {
        return "load: " + std::to_string(m.rejected) + " malformed records";
    }
    if (m.pending_count != 0) {
        return "forget left pending after the boot finished it";
    }
    if (c.last_kept != 0 && c.last_kept != c.last_forgot &&
        nb_memory_get(&m, c.last_kept) == nullptr) {
        return "confirmed fact " + std::to_string(c.last_kept) + " missing";
    }
    if (c.last_forgot != 0 && nb_memory_get(&m, c.last_forgot) != nullptr) {
        return "confirmed forget " + std::to_string(c.last_forgot) + " came back";
    }
    uint32_t prev = 0;
    for (uint16_t i = 0; i < m.episode_count; i++) {
        if (m.episodes[i].id <= prev || m.episodes[i].id >= m.next_id) {
            return "episode ids out of order";
        }
        prev = m.episodes[i].id;
    }
    for (uint16_t i = 0; i < m.long_count; i++) {
        if (m.longs[i].id >= m.next_id) {
            return "long record id beyond next id";
        }
    }
    return "";
}

}  // namespace

PowerCutTest::PowerCutTest(StoreService& store, FaceDisplay* display)
    : store_(store), display_(display) {}

void PowerCutTest::Start() {
#ifdef NB3_POWER_CUT_TEST
    xTaskCreate(TaskEntry, "nb_powercut", 6144, this, 2, nullptr);
#endif
}

void PowerCutTest::TaskEntry(void* arg) {
    static_cast<PowerCutTest*>(arg)->Run();
    vTaskDelete(nullptr);
}

void PowerCutTest::Run() {
    for (int i = 0; i < 60 && !store_.Available(); i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!store_.Available()) {
        ESP_LOGE(TAG, "no card: nothing to test");
        return;
    }
    auto* m = static_cast<nb_memory_t*>(heap_caps_malloc(sizeof(nb_memory_t), MALLOC_CAP_SPIRAM));
    auto* scratch = static_cast<char*>(heap_caps_malloc(NB_MEM_SCRATCH_SIZE, MALLOC_CAP_SPIRAM));
    auto* toks = static_cast<nb_json_tok_t*>(
        heap_caps_malloc(NB_MEM_TOKENS * sizeof(nb_json_tok_t), MALLOC_CAP_SPIRAM));
    if (m == nullptr || scratch == nullptr || toks == nullptr) {
        ESP_LOGE(TAG, "out of memory");
        return;
    }
    const nb_mem_io_t io = {&store_, ReadSlot, WriteSlot, Append, ReadLog, ClearLog};
    nb_clock_t clock;  // never synced: every stamp is of unknown date
    nb_clock_init(&clock, 0, 0, 0);

    Counters c;
    LoadCounters(&c);
    nb_memory_init(m, scratch, NB_MEM_SCRATCH_SIZE, toks, NB_MEM_TOKENS);
    const nb_mem_status_t load = nb_memory_load(m, &io);
    std::string problem = Check(*m, load, c);
    const std::string ini = CheckPersonality(store_, &c);
    if (problem.empty()) {
        problem = ini;
    }
    c.boots++;
    if (!problem.empty()) {
        c.failures++;
    }
    SaveCounters(c);
    char line[160];
    snprintf(line, sizeof(line), "Teste de cortes: boot %lu, %s (%lu falhas)",
             static_cast<unsigned long>(c.boots), problem.empty() ? "ok" : "FALHOU",
             static_cast<unsigned long>(c.failures));
    if (problem.empty()) {
        ESP_LOGI(TAG, "boot %lu ok: %u facts, %u episodes, %u frames, next id %lu; failures "
                 "so far %lu", static_cast<unsigned long>(c.boots), m->long_count,
                 m->episode_count, m->log_frames, static_cast<unsigned long>(m->next_id),
                 static_cast<unsigned long>(c.failures));
    } else {
        ESP_LOGE(TAG, "boot %lu FAILED: %s; failures so far %lu",
                 static_cast<unsigned long>(c.boots), problem.c_str(),
                 static_cast<unsigned long>(c.failures));
    }
    if (display_ != nullptr) {
        display_->ShowNotification(line, 60000);
    }
    if (load != NB_MEM_OK) {
        return;  // nothing safe to write on
    }

    uint32_t compactions = 0;
    for (uint32_t op = 1;; op++) {
        char text[48];
        snprintf(text, sizeof(text), "episodio %lu", static_cast<unsigned long>(op));
        nb_mem_episode_t ep = {NB_EP_AFFECTION, 0.5f, 0.0f, 0.0f, 0, text};
        nb_mem_status_t s = nb_memory_episode(m, &io, &ep, nb_clock_stamp(&clock, 0), &clock, 0,
                                              nullptr);
        // Forgets empty the log, so they are spaced for it to reach a compaction
        // in between.
        if (op % 100 == 13) {
            uint32_t id = 0;
            snprintf(text, sizeof(text), "manter %lu", static_cast<unsigned long>(op));
            s = nb_memory_remember(m, &io, NB_MEM_FACT, nullptr, text, nb_clock_stamp(&clock, 0),
                                   &id);
            if (s == NB_MEM_OK) {
                c.last_kept = id;
                SaveCounters(c);
            }
        } else if (op % 100 == 63) {
            uint32_t id = 0;
            snprintf(text, sizeof(text), "apagar %lu", static_cast<unsigned long>(op));
            s = nb_memory_remember(m, &io, NB_MEM_FACT, nullptr, text, nb_clock_stamp(&clock, 0),
                                   &id);
            if (s == NB_MEM_OK) {
                s = nb_memory_forget_id(m, &io, id, nullptr);
                if (s == NB_MEM_OK) {
                    c.last_forgot = id;
                    SaveCounters(c);
                }
            }
        }
        if (m->long_count > kLongLimit && m->longs[0].id != c.last_kept) {
            nb_memory_forget_id(m, &io, m->longs[0].id, nullptr);
        }
        if (nb_memory_wants_compaction(m)) {
            nb_memory_compact(m, &io, &clock, 0);
            compactions++;
        }
        if (s == NB_MEM_IO) {
            ESP_LOGE(TAG, "card error at op %lu: stopping", static_cast<unsigned long>(op));
            return;
        }
        if (op % 50 == 0) {
            ESP_LOGI(TAG, "op %lu: %u facts, %u episodes, %u frames, %lu compactions",
                     static_cast<unsigned long>(op), m->long_count, m->episode_count,
                     m->log_frames, static_cast<unsigned long>(compactions));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
