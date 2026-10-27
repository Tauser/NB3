#ifndef NB_DRIFT_H
#define NB_DRIFT_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_memory.h"
#include "nb_personality.h"

#ifdef __cplusplus
extern "C" {
#endif

// The personality learning from the routine (docs/VIDA.md §4): the habits
// of the memory (nb_mem_patterns_t) shift the personality from the file a
// little, slowly and within limits. A pure function of the habits and the
// time, so nothing is stored and forgetting the episodes undoes it.
//
// - Warmth: the feeling of the dated episodes per day observed, a hurt
//   weighing twice and a conversation half, moves the temperament's valence
//   up to NB_DRIFT_VALENCE_MAX either way.
// - Company: petting and conversations per day move the initiative up to
//   NB_DRIFT_INITIATIVE_MAX; around NB_DRIFT_COMPANY_MID a day leaves it.
//   An initiative of 0 in the file stays 0: the owner turned it off.
// - Slowly: the shift grows with the days observed and is whole only after
//   NB_DRIFT_FULL_DAYS. Unknown time or nothing dated means no shift.
// - adaptation (from the file, 0..1) scales all of it; 0 turns it off.

#define NB_DRIFT_FULL_DAYS 14.0f
#define NB_DRIFT_VALENCE_MAX 0.10f
#define NB_DRIFT_WARMTH_SCALE 2.0f  // feeling per day for ~76% of the shift
#define NB_DRIFT_HURT_WEIGHT 2.0f
#define NB_DRIFT_CONVERSATION_WEIGHT 0.5f
#define NB_DRIFT_INITIATIVE_MAX 0.15f
#define NB_DRIFT_COMPANY_MID 3.0f   // petting + conversations a day that change nothing
#define NB_DRIFT_COMPANY_SCALE 3.0f

typedef struct {
    float days;             // observed, 0 when unknown
    float confidence;       // 0..1, days / NB_DRIFT_FULL_DAYS
    float warmth_per_day;
    float company_per_day;
    float valence_shift;    // applied to the temperament
    float initiative_shift; // applied to the initiative
} nb_drift_t;

// out = base shifted by the habits, clamped to the safe ranges. Returns
// false (out = base) when there is nothing to learn from.
bool nb_drift_apply(const nb_personality_t *base, const nb_mem_patterns_t *habits, int64_t now_s,
                    bool now_known, nb_personality_t *out, nb_drift_t *why);

#ifdef __cplusplus
}
#endif

#endif  // NB_DRIFT_H
