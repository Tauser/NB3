#include <math.h>
#include <string.h>

#include "nb_drift.h"
#include "nb_test.h"

#define DAY 86400LL
#define SINCE 1792929600LL  // 2026-10-26 12:00

// A routine of `per_day` episodes of one type at 9 h, each with `feeling`,
// observed for `days`.
static nb_mem_patterns_t routine(nb_ep_type_t type, float per_day, float feeling, float days) {
    nb_mem_patterns_t p;
    memset(&p, 0, sizeof(p));
    p.since_s = SINCE;
    const uint32_t n = (uint32_t)(per_day * days + 0.5f);
    p.hours[type][9] = n;
    p.valence_sum[type] = feeling * (float)n;
    return p;
}

static nb_personality_t learn(const nb_personality_t *base, const nb_mem_patterns_t *p,
                              float days, nb_drift_t *why) {
    nb_personality_t out;
    nb_drift_apply(base, p, SINCE + (int64_t)(days * (float)DAY), true, &out, why);
    return out;
}

static void test_nothing_to_learn_leaves_the_file(void) {
    const nb_personality_t base = nb_personality_default();
    nb_mem_patterns_t empty;
    memset(&empty, 0, sizeof(empty));
    nb_personality_t out;
    NB_CHECK(!nb_drift_apply(&base, &empty, SINCE + 30 * DAY, true, &out, NULL));
    NB_CHECK(memcmp(&base, &out, sizeof(out)) == 0);

    // Without the time the days observed are unknown.
    const nb_mem_patterns_t warm = routine(NB_EP_AFFECTION, 5.0f, 0.7f, 30.0f);
    NB_CHECK(!nb_drift_apply(&base, &warm, 0, false, &out, NULL));
    NB_CHECK(memcmp(&base, &out, sizeof(out)) == 0);

    // adaptation = 0 in the file turns it off.
    nb_personality_t fixed = base;
    fixed.adaptation = 0.0f;
    NB_CHECK(!nb_drift_apply(&fixed, &warm, SINCE + 30 * DAY, true, &out, NULL));
    NB_CHECK(memcmp(&fixed, &out, sizeof(out)) == 0);
}

static void test_a_warm_routine_warms_the_temperament_slowly(void) {
    const nb_personality_t base = nb_personality_default();
    nb_drift_t why;
    const nb_mem_patterns_t two = routine(NB_EP_AFFECTION, 5.0f, 0.7f, 2.0f);
    const float early = learn(&base, &two, 2.0f, &why).temperament.valence - base.temperament.valence;
    const nb_mem_patterns_t month = routine(NB_EP_AFFECTION, 5.0f, 0.7f, 30.0f);
    const float late = learn(&base, &month, 30.0f, &why).temperament.valence - base.temperament.valence;
    NB_CHECK(early > 0.0f && early < 0.02f);  // two days: a seventh of the way
    NB_CHECK(late > 0.08f && late <= NB_DRIFT_VALENCE_MAX + 1e-6f);
    NB_CHECK(fabsf(why.confidence - 1.0f) < 1e-6f);

    // Ten times the petting never goes past the limit.
    const nb_mem_patterns_t lots = routine(NB_EP_AFFECTION, 50.0f, 1.0f, 30.0f);
    const float most = learn(&base, &lots, 30.0f, NULL).temperament.valence - base.temperament.valence;
    NB_CHECK(most <= NB_DRIFT_VALENCE_MAX + 1e-6f && most >= late);

    // Nor past the safe range of the file.
    nb_personality_t sunny = base;
    sunny.temperament.valence = 0.45f;
    NB_CHECK(fabsf(learn(&sunny, &lots, 30.0f, NULL).temperament.valence - 0.5f) < 1e-6f);
}

static void test_hurt_weighs_more_than_petting(void) {
    const nb_personality_t base = nb_personality_default();
    nb_mem_patterns_t p = routine(NB_EP_AFFECTION, 2.0f, 0.5f, 30.0f);
    p.hours[NB_EP_HURT][20] = 60;  // 2 a day
    p.valence_sum[NB_EP_HURT] = -0.5f * 60.0f;
    nb_drift_t why;
    const nb_personality_t out = learn(&base, &p, 30.0f, &why);
    NB_CHECK(why.warmth_per_day < 0.0f);  // as many hurts as caresses: it sours
    NB_CHECK(out.temperament.valence < base.temperament.valence);
    NB_CHECK(out.temperament.valence >= base.temperament.valence - NB_DRIFT_VALENCE_MAX - 1e-6f);
}

static void test_company_moves_the_initiative(void) {
    const nb_personality_t base = nb_personality_default();
    const nb_mem_patterns_t busy = routine(NB_EP_CONVERSATION, 10.0f, 0.3f, 30.0f);
    const nb_mem_patterns_t lonely = routine(NB_EP_CONVERSATION, 0.2f, 0.3f, 30.0f);
    const nb_mem_patterns_t usual = routine(NB_EP_CONVERSATION, NB_DRIFT_COMPANY_MID, 0.3f, 30.0f);
    NB_CHECK(learn(&base, &busy, 30.0f, NULL).initiative > base.initiative + 0.1f);
    NB_CHECK(learn(&base, &lonely, 30.0f, NULL).initiative < base.initiative - 0.05f);
    NB_CHECK(fabsf(learn(&base, &usual, 30.0f, NULL).initiative - base.initiative) < 0.01f);

    // Off in the file stays off; low in the file never reaches off.
    nb_personality_t off = base;
    off.initiative = 0.0f;
    NB_CHECK(learn(&off, &busy, 30.0f, NULL).initiative == 0.0f);
    nb_personality_t shy = base;
    shy.initiative = 0.05f;
    NB_CHECK(learn(&shy, &lonely, 30.0f, NULL).initiative > 0.0f);
}

static void test_undated_episodes_do_not_count_as_days(void) {
    const nb_personality_t base = nb_personality_default();
    nb_mem_patterns_t p = routine(NB_EP_AFFECTION, 1.0f, 0.5f, 30.0f);
    nb_drift_t dated_only;
    learn(&base, &p, 30.0f, &dated_only);
    p.undated[NB_EP_AFFECTION] = 300;
    p.valence_sum[NB_EP_AFFECTION] += 0.5f * 300.0f;
    nb_drift_t with_undated;
    learn(&base, &p, 30.0f, &with_undated);
    NB_CHECK(fabsf(dated_only.warmth_per_day - with_undated.warmth_per_day) < 1e-4f);
    NB_CHECK(fabsf(dated_only.company_per_day - with_undated.company_per_day) < 1e-4f);
}

int main(void) {
    NB_RUN(test_nothing_to_learn_leaves_the_file);
    NB_RUN(test_a_warm_routine_warms_the_temperament_slowly);
    NB_RUN(test_hurt_weighs_more_than_petting);
    NB_RUN(test_company_moves_the_initiative);
    NB_RUN(test_undated_episodes_do_not_count_as_days);
    NB_TEST_MAIN_END();
}
