#ifndef NB3_PERSONALITY_DRIFT_H
#define NB3_PERSONALITY_DRIFT_H

#include "behavior/initiative_modifier.h"
#include "behavior/mood_modifier.h"
#include "core/nb_drift.h"
#include "core/nb_memory.h"
#include "core/nb_personality.h"

// The personality learning from the routine (core/nb_drift, docs/VIDA.md
// §4): the memory hands over its habits about once a minute and this pushes
// the shifted temperament to the mood and the shifted initiative to the
// initiative. The file's personality is the base and is never overwritten.
class PersonalityDrift {
public:
    PersonalityDrift(const nb_personality_t& base, MoodModifier& mood,
                     InitiativeModifier& initiative);

    PersonalityDrift(const PersonalityDrift&) = delete;
    PersonalityDrift& operator=(const PersonalityDrift&) = delete;

    // On the memory task.
    void OnHabits(const nb_mem_patterns_t& habits, int64_t now_s, bool now_known);

private:
    const nb_personality_t base_;
    MoodModifier& mood_;
    InitiativeModifier& initiative_;
    nb_personality_t shown_;  // last pushed
};

#endif  // NB3_PERSONALITY_DRIFT_H
