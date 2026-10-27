#ifndef NB3_ENERGY_MODIFIER_H
#define NB3_ENERGY_MODIFIER_H

#include <atomic>
#include <mutex>

#include <esp_timer.h>

#include "behavior/behavior_modifier.h"
#include "behavior/expression_arbiter.h"
#include "behavior/initiative_modifier.h"
#include "core/nb_energy.h"
#include "face/face_display.h"
#include "memory/clock_service.h"

// Energy, boredom and the time of day (core/nb_energy, docs/VIDA.md §3.3).
// Once a second it reads the behavioral state and the clock, and turns what
// the core says into manner: a calmer gaze as the night comes, fewer thoughts
// on its own when tired and more when bored, the sleepy face at rest, and
// sleep after a quiet night. Waking is the behavioral state's business: a
// touch or the wake word leave SLEEPING, and the energy follows.
class EnergyModifier {
public:
    EnergyModifier(ExpressionArbiter& arbiter, BehaviorModifier& behavior, ClockService& clock,
                   InitiativeModifier& initiative, FaceDisplay* display);
    ~EnergyModifier();

    EnergyModifier(const EnergyModifier&) = delete;
    EnergyModifier& operator=(const EnergyModifier&) = delete;

    struct Snapshot {
        float energy, boredom;
        bool drowsy, sleeping, time_known;
    };
    Snapshot Read();

private:
    static void OnTimer(void* arg);
    void Tick();
    int MinuteOfDay();

    ExpressionArbiter& arbiter_;
    BehaviorModifier& behavior_;
    ClockService& clock_;
    InitiativeModifier& initiative_;
    FaceDisplay* display_;
    std::mutex mutex_;
    nb_energy_t energy_;     // under mutex_
    bool time_known_ = false;
    bool drowsy_shown_ = false;  // esp_timer task only
    std::atomic<bool> sleeping_{false};
    uint32_t last_ms_ = 0;
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_ENERGY_MODIFIER_H
