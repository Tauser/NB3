#include "mood_modifier.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "Mood"

namespace {

constexpr uint32_t kTickPeriodMs = 200;

// Every stimulus the mood receives: its push (valence, arousal), the most it
// may move the mood per habituation window, and the weight kept per repeat
// (0 = default halving). Petting is a sustained pleasure, so its pulses fade
// slowly; being listened to only nudges alertness, never pleasure. Tuned on
// the bench.
constexpr nb_affect_stimulus_t kTap = {"touch:tap", 0.12f, 0.08f, 0.35f, 0.0f};
// Petting: ~15 s of it keeps the robot loving for ~30 s after release.
constexpr nb_affect_stimulus_t kPetting = {"touch:pet", 0.18f, -0.07f, 1.00f, 0.7f};
constexpr nb_affect_stimulus_t kListening = {"conv:listen", 0.02f, 0.05f, 0.20f, 0.0f};
constexpr nb_affect_stimulus_t kFault = {"device:fault", -0.30f, 0.30f, 0.60f, 0.0f};

constexpr float kEmotionCap = 0.45f;

struct EmotionPush {
    nb_expression_t expression;
    const char* key;
    float valence;
    float arousal;
};

// Emotions of the conversation. Listening, thinking and neutral say nothing
// about how the robot feels and are absent.
constexpr EmotionPush kEmotions[] = {
    {NB_EXPR_HAPPY, "xiaozhi:happy", 0.20f, 0.10f},
    {NB_EXPR_EXCITED, "xiaozhi:excited", 0.20f, 0.25f},
    {NB_EXPR_LOVING, "xiaozhi:loving", 0.25f, -0.05f},
    {NB_EXPR_CURIOUS, "xiaozhi:curious", 0.05f, 0.10f},
    {NB_EXPR_SURPRISED, "xiaozhi:surprised", 0.00f, 0.20f},
    {NB_EXPR_CONFUSED, "xiaozhi:confused", -0.05f, 0.10f},
    {NB_EXPR_EMBARRASSED, "xiaozhi:embarrassed", -0.05f, 0.10f},
    {NB_EXPR_SAD, "xiaozhi:sad", -0.20f, -0.10f},
    {NB_EXPR_ANGRY, "xiaozhi:angry", -0.20f, 0.20f},
    {NB_EXPR_SLEEPY, "xiaozhi:sleepy", 0.00f, -0.15f},
    {NB_EXPR_DIZZY, "xiaozhi:dizzy", -0.05f, 0.10f},
};

uint32_t NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

}  // namespace

MoodModifier::MoodModifier(ExpressionArbiter& arbiter, const nb_personality_t& personality)
    : arbiter_(arbiter) {
    const nb_affect_personality_t p = nb_personality_affect(&personality);
    nb_affect_init(&affect_, &p);
    ESP_LOGI(TAG, "personality: temperament (%.2f, %.2f), reactivity %.2f, recovery %lu s, "
             "negative x%.1f, initiative %.2f",
             personality.temperament.valence, personality.temperament.arousal,
             personality.reactivity, static_cast<unsigned long>(personality.recovery_ms / 1000),
             personality.negative_slowdown, personality.initiative);
    if (xTaskCreate(TaskEntry, "nb_affect", 3072, this, 3, &task_) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task not created: the mood will not decay or show");
    }
}

MoodModifier::~MoodModifier() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
}

void MoodModifier::OnEmotion(nb_expression_t expression) {
    for (const auto& push : kEmotions) {
        if (push.expression == expression) {
            Feel({push.key, push.valence, push.arousal, kEmotionCap, 0.0f});
            return;
        }
    }
}

void MoodModifier::OnTap() { Feel(kTap); }
void MoodModifier::OnPetting() { Feel(kPetting); }
void MoodModifier::OnListening() { Feel(kListening); }
void MoodModifier::OnFault() { Feel(kFault); }

void MoodModifier::Feel(const nb_affect_stimulus_t& stimulus) {
    std::lock_guard<std::mutex> lock(mutex_);
    float applied = nb_affect_stimulate(&affect_, &stimulus, NowMs());
    ESP_LOGI(TAG, "%s x%.2f -> v=%.2f a=%.2f", stimulus.key, applied, affect_.mood.valence,
             affect_.mood.arousal);
}

void MoodModifier::TaskEntry(void* arg) {
    static_cast<MoodModifier*>(arg)->Run();
}

void MoodModifier::Run() {
    uint32_t last_ms = NowMs();
    while (true) {
        const uint32_t now = NowMs();
        nb_expression_t expression;
        nb_affect_vec_t mood;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            nb_affect_tick(&affect_, now - last_ms);
            expression = nb_affect_resolve(&affect_);
            mood = affect_.mood;
        }
        last_ms = now;
        if (expression != shown_) {
            ESP_LOGI(TAG, "shows %s (v=%.2f a=%.2f)", nb_expression_name(expression),
                     mood.valence, mood.arousal);
            shown_ = expression;
            if (expression == NB_EXPR_NEUTRAL) {
                arbiter_.Withdraw(NB_SOURCE_MOOD);
            } else {
                nb_proposal_t proposal = {expression, priority::kMood, 0, false};
                arbiter_.Propose(NB_SOURCE_MOOD, proposal);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kTickPeriodMs));
    }
}
