#include "memory_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/semphr.h>

#define TAG "Memory"

namespace {

constexpr const char* kRecord = "memoria";     // snapshot slots
constexpr const char* kLog = "episodios.log";  // episode frames since the snapshot

constexpr TickType_t kTickPeriod = pdMS_TO_TICKS(10000);
// The bias follows the episodes slowly; a clock sync recomputes it at once.
constexpr uint32_t kBiasPeriodMs = 60 * 1000;
// Recall counts and rebased stamps are written in batches.
constexpr uint32_t kFlushPeriodMs = 5 * 60 * 1000;
constexpr float kBiasLogStep = 0.01f;

// Petting shorter than this is a touch, not an episode; longer petting
// matters more, up to kPettingFullMs.
constexpr uint32_t kPettingMinMs = 3000;
constexpr uint32_t kPettingFullMs = 30000;
constexpr uint32_t kConversationMinMs = 5000;
constexpr uint32_t kConversationFullMs = 2 * 60 * 1000;

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

float Ramp(uint32_t ms, uint32_t full_ms) {
    return std::min(1.0f, static_cast<float>(ms) / static_cast<float>(full_ms));
}

void DurationText(const char* what, uint32_t ms, char* out, size_t cap) {
    const unsigned s = static_cast<unsigned>(ms / 1000);
    if (s < 120) {
        snprintf(out, cap, "%s de %u s", what, s);
    } else {
        snprintf(out, cap, "%s de %u min", what, s / 60);
    }
}

const char* StatusName(nb_mem_status_t s) {
    switch (s) {
        case NB_MEM_OK: return "ok";
        case NB_MEM_UPDATED: return "updated";
        case NB_MEM_NOT_FOUND: return "not found";
        case NB_MEM_FULL: return "full";
        case NB_MEM_INVALID: return "invalid";
        case NB_MEM_IO: return "card error";
        case NB_MEM_UNREADABLE: return "unreadable snapshot";
    }
    return "?";
}

}  // namespace

struct MemoryService::Job {
    const std::function<void(const Access&)>* fn;
    SemaphoreHandle_t done;
    bool ran;
};

struct MemoryService::Message {
    enum Type : uint8_t { kCard, kPetting, kConversation, kQuery } type;
    bool available;
    uint32_t duration_ms;
    nb_affect_vec_t feeling;
    Job* job;
};

MemoryService::MemoryService(StoreService& store, ClockService& clock, MoodModifier& mood)
    : store_(store), clock_(clock), mood_(mood) {
    io_ = {this, IoReadSlot, IoWriteSlot, IoAppend, IoReadLog, IoClearLog};
    // About 230 KB with the JSON buffers: PSRAM.
    memory_ = static_cast<nb_memory_t*>(heap_caps_malloc(sizeof(nb_memory_t), MALLOC_CAP_SPIRAM));
    auto* scratch = static_cast<char*>(heap_caps_malloc(NB_MEM_SCRATCH_SIZE, MALLOC_CAP_SPIRAM));
    auto* toks = static_cast<nb_json_tok_t*>(
        heap_caps_malloc(NB_MEM_TOKENS * sizeof(nb_json_tok_t), MALLOC_CAP_SPIRAM));
    queue_ = xQueueCreate(16, sizeof(Message));
    if (memory_ == nullptr || scratch == nullptr || toks == nullptr || queue_ == nullptr) {
        ESP_LOGE(TAG, "out of memory: the robot will not remember");
        return;
    }
    nb_memory_init(memory_, scratch, NB_MEM_SCRATCH_SIZE, toks, NB_MEM_TOKENS);
    if (xTaskCreate(TaskEntry, "nb_memory", 6144, this, 2, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task not created: the robot will not remember");
    }
}

MemoryService::~MemoryService() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    if (memory_ != nullptr) {
        heap_caps_free(memory_->scratch);
        heap_caps_free(memory_->toks);
        heap_caps_free(memory_);
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
    }
}

void MemoryService::OnCard(bool available) {
    Message msg = {};
    msg.type = Message::kCard;
    msg.available = available;
    Post(msg);
}

void MemoryService::OnPetting(uint32_t duration_ms) {
    if (duration_ms < kPettingMinMs) {
        return;
    }
    Message msg = {};
    msg.type = Message::kPetting;
    msg.duration_ms = duration_ms;
    Post(msg);
}

void MemoryService::OnConversation(uint32_t duration_ms, nb_affect_vec_t feeling) {
    if (duration_ms < kConversationMinMs) {
        return;
    }
    Message msg = {};
    msg.type = Message::kConversation;
    msg.duration_ms = duration_ms;
    msg.feeling = feeling;
    Post(msg);
}

bool MemoryService::Query(const std::function<void(const Access&)>& fn) {
    if (task_ == nullptr) {
        return false;
    }
    Job job = {&fn, xSemaphoreCreateBinary(), false};
    if (job.done == nullptr) {
        return false;
    }
    Message msg = {};
    msg.type = Message::kQuery;
    msg.job = &job;
    bool ok = xQueueSend(queue_, &msg, pdMS_TO_TICKS(1000)) == pdTRUE;
    if (ok) {
        xSemaphoreTake(job.done, portMAX_DELAY);
    }
    vSemaphoreDelete(job.done);
    return ok && job.ran;
}

// Never blocks the caller (touch, xiaozhi, store tasks).
void MemoryService::Post(const Message& msg) {
    if (task_ == nullptr || xQueueSend(queue_, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event dropped (type %d)", msg.type);
    }
}

void MemoryService::TaskEntry(void* arg) {
    static_cast<MemoryService*>(arg)->Run();
}

void MemoryService::Run() {
    while (true) {
        Message msg;
        if (xQueueReceive(queue_, &msg, kTickPeriod) == pdTRUE) {
            Handle(msg);
        }
        Tick();
    }
}

void MemoryService::Handle(const Message& msg) {
    switch (msg.type) {
        case Message::kCard:
            if (msg.available && !loaded_) {
                Load();
            } else if (!msg.available && loaded_) {
                // The card is the truth: whatever comes back is read again.
                ESP_LOGW(TAG, "card gone: memory off until it is back");
                loaded_ = false;
                mood_.SetBias(0.0f, 0.0f);
                bias_ = {0.0f, 0.0f};
            }
            break;
        case Message::kPetting: {
            char text[NB_MEM_TEXT_MAX];
            DurationText("carinho", msg.duration_ms, text, sizeof(text));
            const float ramp = Ramp(msg.duration_ms, kPettingFullMs);
            nb_mem_episode_t ep = {NB_EP_AFFECTION, 0.7f, -0.1f, 0.3f + 0.4f * ramp, 0, text};
            AddEpisode(ep);
            break;
        }
        case Message::kConversation: {
            char text[NB_MEM_TEXT_MAX];
            DurationText("conversa", msg.duration_ms, text, sizeof(text));
            const float ramp = Ramp(msg.duration_ms, kConversationFullMs);
            // The mood's deviation is small (a fraction of the axis): scale
            // it so a warm conversation reads as clearly warm.
            const float v = std::clamp(msg.feeling.valence * 2.0f, -1.0f, 1.0f);
            const float a = std::clamp(msg.feeling.arousal * 2.0f, -1.0f, 1.0f);
            nb_mem_episode_t ep = {NB_EP_CONVERSATION, v, a, 0.3f + 0.3f * ramp, 0, text};
            AddEpisode(ep);
            break;
        }
        case Message::kQuery: {
            if (loaded_) {
                nb_clock_t clock;
                uint64_t now = 0;
                clock_.Snapshot(&clock, &now);
                (*msg.job->fn)(Access{*memory_, io_, clock, now});
                UpdateBias(clock, now);  // a forget may have taken episodes away
                msg.job->ran = true;
            }
            xSemaphoreGive(msg.job->done);
            break;
        }
    }
}

void MemoryService::Load() {
    nb_memory_init(memory_, memory_->scratch, memory_->scratch_cap, memory_->toks,
                   memory_->toks_cap);
    const uint32_t start = NowMs();
    const nb_mem_status_t s = nb_memory_load(memory_, &io_);
    if (s == NB_MEM_IO) {
        ESP_LOGE(TAG, "load failed: card error");
        return;
    }
    loaded_ = true;
    last_flush_ms_ = NowMs();
    clock_syncs_ = UINT32_MAX;  // rebase and bias on the next tick
    last_bias_ms_ = 0;
    if (s == NB_MEM_UNREADABLE) {
        ESP_LOGE(TAG, "snapshot on the card is not readable by this firmware: memory is "
                 "read-only and empty");
        return;
    }
    ESP_LOGI(TAG, "loaded %u long records, %u episodes, %u frames, next id %lu, %u rejected "
             "(%lu ms)",
             memory_->long_count, memory_->episode_count, memory_->log_frames,
             static_cast<unsigned long>(memory_->next_id), memory_->rejected,
             static_cast<unsigned long>(NowMs() - start));
}

void MemoryService::AddEpisode(const nb_mem_episode_t& ep) {
    if (!loaded_) {
        return;  // no card, no memory
    }
    nb_clock_t clock;
    uint64_t now = 0;
    clock_.Snapshot(&clock, &now);
    uint32_t id = 0;
    const nb_mem_status_t s =
        nb_memory_episode(memory_, &io_, &ep, nb_clock_stamp(&clock, now), &clock, now, &id);
    ESP_LOGI(TAG, "episode %lu %s \"%s\" v=%.2f a=%.2f importance %.2f: %s",
             static_cast<unsigned long>(id), nb_memory_episode_name(ep.type), ep.text,
             ep.valence, ep.arousal, ep.importance, StatusName(s));
    UpdateBias(clock, now);
}

void MemoryService::UpdateBias(const nb_clock_t& clock, uint64_t now_ms) {
    nb_affect_vec_t bias;
    nb_memory_bias(memory_, &clock, now_ms, &bias.valence, &bias.arousal);
    last_bias_ms_ = NowMs();
    if (std::fabs(bias.valence - bias_.valence) >= kBiasLogStep ||
        std::fabs(bias.arousal - bias_.arousal) >= kBiasLogStep) {
        ESP_LOGI(TAG, "mood bias v=%+.3f a=%+.3f", bias.valence, bias.arousal);
    }
    bias_ = bias;
    mood_.SetBias(bias.valence, bias.arousal);
}

void MemoryService::Tick() {
    if (!loaded_) {
        return;
    }
    nb_clock_t clock;
    uint64_t now = 0;
    clock_.Snapshot(&clock, &now);
    const uint32_t ms = NowMs();
    if (clock.syncs != clock_syncs_) {
        clock_syncs_ = clock.syncs;
        if (nb_memory_rebase(memory_, &clock)) {
            ESP_LOGI(TAG, "this boot's records dated by the clock (%s)",
                     nb_clock_level_name(clock.level));
        }
        UpdateBias(clock, now);
    } else if (ms - last_bias_ms_ >= kBiasPeriodMs) {
        UpdateBias(clock, now);
    }
    if (nb_memory_wants_compaction(memory_)) {
        const uint16_t before = memory_->episode_count;
        const nb_mem_status_t s = nb_memory_compact(memory_, &io_, &clock, now);
        uint32_t counted = 0;
        for (int t = 0; t < NB_EP_TYPE_COUNT; t++) {
            counted += nb_memory_patterns_total(&memory_->patterns, static_cast<nb_ep_type_t>(t));
        }
        ESP_LOGI(TAG, "compacted: %u -> %u episodes, %lu in the habit counts: %s", before,
                 memory_->episode_count, static_cast<unsigned long>(counted), StatusName(s));
        last_flush_ms_ = ms;
    } else if ((memory_->dirty || memory_->pending_count > 0) &&
               ms - last_flush_ms_ >= kFlushPeriodMs) {
        const nb_mem_status_t s = nb_memory_flush(memory_, &io_);
        ESP_LOGI(TAG, "snapshot written: %s", StatusName(s));
        last_flush_ms_ = ms;
    }
}

// --- io over the store task --------------------------------------------------

bool MemoryService::IoReadSlot(void* ctx, nb_mem_visit_t visit, void* arg, bool* found) {
    auto* self = static_cast<MemoryService*>(ctx);
    std::vector<uint8_t> payload;
    nb_slot_info_t info;
    if (!self->store_.ReadSlot(kRecord, &payload, &info)) {
        *found = false;
        return self->store_.Available();  // no slot yet, unless the card failed
    }
    *found = true;
    visit(payload.data(), static_cast<uint32_t>(payload.size()), info.max_seq, arg);
    return true;
}

bool MemoryService::IoWriteSlot(void* ctx, const void* payload, uint32_t length,
                                uint32_t max_seq) {
    return static_cast<MemoryService*>(ctx)->store_.WriteSlot(kRecord, payload, length, max_seq);
}

bool MemoryService::IoAppend(void* ctx, uint32_t seq, const void* payload, uint32_t length) {
    return static_cast<MemoryService*>(ctx)->store_.AppendFrame(kLog, seq, payload, length);
}

namespace {

struct LogVisit {
    nb_mem_visit_t visit;
    void* arg;
};

// Runs on the store task while the memory task waits for it.
bool VisitFrame(uint32_t seq, const uint8_t* payload, uint32_t length, void* ctx) {
    auto* v = static_cast<LogVisit*>(ctx);
    return v->visit(payload, length, seq, v->arg);
}

}  // namespace

bool MemoryService::IoReadLog(void* ctx, nb_mem_visit_t visit, void* arg) {
    LogVisit v = {visit, arg};
    return static_cast<MemoryService*>(ctx)->store_.ReadLog(kLog, VisitFrame, &v, nullptr);
}

bool MemoryService::IoClearLog(void* ctx) {
    return static_cast<MemoryService*>(ctx)->store_.ClearLog(kLog);
}
