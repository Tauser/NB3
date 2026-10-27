#include "nb_drift.h"

#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t dated(const nb_mem_patterns_t *p, nb_ep_type_t type) {
    uint32_t n = 0;
    for (int h = 0; h < NB_MEM_HOURS; h++) {
        n += p->hours[type][h];
    }
    return n;
}

// The valence sum covers undated episodes too; only the dated share counts,
// as only those fall inside the days observed.
static float dated_feeling(const nb_mem_patterns_t *p, nb_ep_type_t type) {
    const uint32_t total = nb_memory_patterns_total(p, type);
    return total == 0 ? 0.0f
                      : p->valence_sum[type] * (float)dated(p, type) / (float)total;
}

bool nb_drift_apply(const nb_personality_t *base, const nb_mem_patterns_t *habits, int64_t now_s,
                    bool now_known, nb_personality_t *out, nb_drift_t *why) {
    nb_drift_t d;
    memset(&d, 0, sizeof(d));
    *out = *base;
    if (why != NULL) {
        *why = d;
    }
    if (!now_known || habits->since_s == 0 || base->adaptation <= 0.0f) {
        return false;
    }
    d.days = (float)(now_s > habits->since_s ? now_s - habits->since_s : 0) / 86400.0f;
    d.confidence = clampf(d.days / NB_DRIFT_FULL_DAYS, 0.0f, 1.0f);
    // A first day still counts as a whole day, so one evening is no habit.
    const float per_day = 1.0f / (d.days > 1.0f ? d.days : 1.0f);
    const float gain = d.confidence * clampf(base->adaptation, 0.0f, 1.0f);

    const float warmth = dated_feeling(habits, NB_EP_AFFECTION) +
                         NB_DRIFT_CONVERSATION_WEIGHT * dated_feeling(habits, NB_EP_CONVERSATION) +
                         NB_DRIFT_HURT_WEIGHT * dated_feeling(habits, NB_EP_HURT);
    d.warmth_per_day = warmth * per_day;
    d.valence_shift = NB_DRIFT_VALENCE_MAX * tanhf(d.warmth_per_day / NB_DRIFT_WARMTH_SCALE) * gain;

    d.company_per_day =
        (float)(dated(habits, NB_EP_AFFECTION) + dated(habits, NB_EP_CONVERSATION)) * per_day;
    if (base->initiative > 0.0f) {
        d.initiative_shift = NB_DRIFT_INITIATIVE_MAX *
                             tanhf((d.company_per_day - NB_DRIFT_COMPANY_MID) /
                                   NB_DRIFT_COMPANY_SCALE) *
                             gain;
    }

    out->temperament.valence += d.valence_shift;
    out->initiative += d.initiative_shift;
    nb_personality_clamp(out);
    if (out->initiative <= 0.0f && base->initiative > 0.0f) {
        out->initiative = 0.01f;  // learning never turns the initiative off
    }
    if (why != NULL) {
        *why = d;
    }
    return true;
}
