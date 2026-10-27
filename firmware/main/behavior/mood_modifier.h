#ifndef NB3_MOOD_MODIFIER_H
#define NB3_MOOD_MODIFIER_H

#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "behavior/expression_arbiter.h"
#include "core/nb_affect.h"
#include "core/nb_personality.h"

// The robot's mood (core/nb_affect). Conversation, touch and device state
// report what happened; the mood integrates it with habituation, decays it
// toward the temperament, and proposes the expression it resolves to as the
// MOOD source. That proposal is not transient, so it is what the face shows
// at rest, including after IDLE clears everything else. The values of every
// stimulus live in mood_modifier.cc, in one place.
class MoodModifier {
public:
    MoodModifier(ExpressionArbiter& arbiter, const nb_personality_t& personality);
    ~MoodModifier();

    MoodModifier(const MoodModifier&) = delete;
    MoodModifier& operator=(const MoodModifier&) = delete;

    void OnEmotion(nb_expression_t expression);  // emotion of the conversation
    void OnTap();
    void OnPetting();                             // start of petting and each pulse
    void OnListening();
    void OnFault();
    // The conversation reports something that touched the robot (-1..1 per
    // axis, scaled down and capped below touch).
    void OnConversationEvent(float valence, float arousal, const std::string& reason);
    // A turning point of an arc (core/nb_arc): the sulk, the forgiving, the
    // making up. Not habituated: an arc already has its own cooldown.
    void OnArc(float valence, float arousal, const char* arc);

    struct Snapshot {
        nb_affect_vec_t mood, temperament, bias;
        nb_expression_t expression;  // what the mood shows
        std::string recent;          // latest stimulus
        uint32_t recent_age_ms;
    };
    Snapshot Read();
    nb_affect_vec_t Mood();  // cheap, for every frame

    // Shift of the resting point from the memory's recent episodes.
    void SetBias(float valence, float arousal);
    // How far the mood is from where it would rest now (temperament + bias):
    // what recent events did to it.
    nb_affect_vec_t Deviation();

private:
    static void TaskEntry(void* arg);
    void Run();
    void Feel(const nb_affect_stimulus_t& stimulus);

    ExpressionArbiter& arbiter_;
    std::mutex mutex_;
    nb_affect_t affect_;
    nb_expression_t shown_ = NB_EXPR_NEUTRAL;
    std::string recent_;  // under mutex_
    uint32_t recent_ms_ = 0;
    TaskHandle_t task_ = nullptr;
};

#endif  // NB3_MOOD_MODIFIER_H
