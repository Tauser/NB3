#include "personality_drift.h"

#include <cmath>

#include <esp_log.h>

#define TAG "Drift"

namespace {

// Smaller changes are not pushed or logged: the drift moves over days.
constexpr float kStep = 0.005f;

}  // namespace

PersonalityDrift::PersonalityDrift(const nb_personality_t& base, MoodModifier& mood,
                                   InitiativeModifier& initiative)
    : base_(base), mood_(mood), initiative_(initiative), shown_(base) {
    ESP_LOGI(TAG, "adaptation %.2f", base_.adaptation);
}

void PersonalityDrift::OnHabits(const nb_mem_patterns_t& habits, int64_t now_s, bool now_known) {
    nb_personality_t learned;
    nb_drift_t why;
    nb_drift_apply(&base_, &habits, now_s, now_known, &learned, &why);
    if (std::fabs(learned.temperament.valence - shown_.temperament.valence) < kStep &&
        std::fabs(learned.initiative - shown_.initiative) < kStep) {
        return;
    }
    ESP_LOGI(TAG, "temperament v %.2f -> %.2f, initiative %.2f -> %.2f (%.1f days, "
             "confidence %.2f, warmth %.2f/day, company %.1f/day)",
             base_.temperament.valence, learned.temperament.valence, base_.initiative,
             learned.initiative, why.days, why.confidence, why.warmth_per_day,
             why.company_per_day);
    mood_.SetTemperament(learned.temperament);
    initiative_.SetInitiative(learned.initiative);
    shown_ = learned;
}
