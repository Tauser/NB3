#ifndef NB3_ARC_MODIFIER_H
#define NB3_ARC_MODIFIER_H

#include <atomic>
#include <mutex>

#include <esp_timer.h>

#include "behavior/behavior_modifier.h"
#include "behavior/energy_modifier.h"
#include "behavior/expression_arbiter.h"
#include "behavior/mood_modifier.h"
#include "core/nb_arc.h"
#include "face/face_display.h"

// Arcs (core/nb_arc, docs/VIDA.md §3.4): sulk and forgive after quick taps,
// making up when a sad robot is petted, and looking around for someone
// after long boredom. Every 50 ms it samples the behavioral state, the mood
// and the energy, proposes the arc's face as source ARC, hands the face its
// look and slow blink, and pushes the mood at the arc's turning points.
class ArcModifier {
public:
    ArcModifier(ExpressionArbiter& arbiter, BehaviorModifier& behavior, MoodModifier& mood,
                EnergyModifier& energy, FaceDisplay* display);
    ~ArcModifier();

    ArcModifier(const ArcModifier&) = delete;
    ArcModifier& operator=(const ArcModifier&) = delete;

    // From the touch task.
    void OnTap();
    void SetPetting(bool petting) { petting_ = petting; }

private:
    static void OnTimer(void* arg);
    void Tick();

    ExpressionArbiter& arbiter_;
    BehaviorModifier& behavior_;
    MoodModifier& mood_;
    EnergyModifier& energy_;
    FaceDisplay* display_;
    std::mutex mutex_;
    nb_arc_t arc_;  // under mutex_
    std::atomic<bool> petting_{false};
    // esp_timer task only.
    nb_arc_kind_t shown_kind_ = NB_ARC_NONE;
    nb_expression_t shown_expression_ = NB_EXPR_NEUTRAL;
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_ARC_MODIFIER_H
