#ifndef NB3_INITIATIVE_MODIFIER_H
#define NB3_INITIATIVE_MODIFIER_H

#include <esp_timer.h>

#include "behavior/behavior_modifier.h"
#include "behavior/expression_arbiter.h"
#include "core/nb_initiative.h"
#include "core/nb_personality.h"

// Initiative at rest (core/nb_initiative, docs/FACE.md): once the robot has
// been quietly at rest for a while it now and then thinks on its own for a
// few seconds, as often as the personality's initiative says. Rest is the
// behavioral state IDLE; anything else (conversation, touch, sleep, fault)
// ends the thought and starts the quiet over.
class InitiativeModifier {
public:
    InitiativeModifier(ExpressionArbiter& arbiter, BehaviorModifier& behavior,
                       const nb_personality_t& personality);
    ~InitiativeModifier();

    InitiativeModifier(const InitiativeModifier&) = delete;
    InitiativeModifier& operator=(const InitiativeModifier&) = delete;

private:
    static void OnTimer(void* arg);
    void Tick();

    ExpressionArbiter& arbiter_;
    BehaviorModifier& behavior_;
    nb_initiative_t initiative_;  // esp_timer task only
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_INITIATIVE_MODIFIER_H
