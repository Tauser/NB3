#ifndef NB3_HEAD_BEHAVIOR_H
#define NB3_HEAD_BEHAVIOR_H

#include <atomic>
#include <mutex>

#include <esp_timer.h>

#include "behavior/behavior_modifier.h"
#include "behavior/energy_modifier.h"
#include "core/nb_head_follow.h"
#include "face/face_display.h"
#include "motion/motion_service.h"

// The head's own behavior (core/nb_head_follow): every 100 ms it reads where
// the eyes look, the behavioral state, sleep and petting, and hands the moves
// the core decides to the motion task, which plays them through the safety.
// The AI comes first: its moves pause this for a while, and after it relaxes
// the head this stays off until the AI moves the head again, so a relaxed
// head stays free.
class HeadBehavior {
public:
    HeadBehavior(MotionService& motion, FaceDisplay* display, BehaviorModifier& behavior,
                 EnergyModifier& energy);
    ~HeadBehavior();

    HeadBehavior(const HeadBehavior&) = delete;
    HeadBehavior& operator=(const HeadBehavior&) = delete;

    // From the MCP tools.
    void OnAiMove();
    void OnAiRelax();
    // From the touch.
    void OnTap();
    void SetPetting(bool petting) { petting_ = petting; }

private:
    static void OnTimer(void* arg);
    void Tick();

    MotionService& motion_;
    FaceDisplay* display_;
    BehaviorModifier& behavior_;
    EnergyModifier& energy_;
    std::mutex mutex_;  // follow_
    nb_head_follow_t follow_;
    std::atomic<bool> petting_{false};
    std::atomic<bool> resting_{false};  // relaxed by the AI: no moves of its own
    esp_timer_handle_t timer_ = nullptr;
};

#endif  // NB3_HEAD_BEHAVIOR_H
