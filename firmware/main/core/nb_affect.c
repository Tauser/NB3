#include "nb_affect.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

// ln(20): a deviation multiplied by exp(-t / tau) reaches 5% at t = recovery.
#define NB_AFFECT_LN20 2.9957323f
#define NB_AFFECT_DEFAULT_HABITUATION 0.5f
// Past this many repeats the habituated share is small; the cap ends it.
#define NB_AFFECT_MAX_REPEATS 32u

// Distance from the temperament at which the mood starts to show, and the
// smaller one at which it goes back to neutral.
#define NB_AFFECT_SHOW_DISTANCE 0.30f
#define NB_AFFECT_HIDE_DISTANCE 0.22f
// A new region must be this much closer than the current one to take over.
#define NB_AFFECT_SWITCH_MARGIN 0.08f

typedef struct {
    nb_expression_t expression;
    nb_affect_vec_t at;
} anchor_t;

// The mood speaks through four regions only: the rest of the vocabulary is
// for the conversation, reflexes and commands.
static const anchor_t kAnchors[] = {
    {NB_EXPR_HAPPY, {0.55f, 0.35f}},
    {NB_EXPR_LOVING, {0.55f, -0.35f}},  // warm and calm: content
    {NB_EXPR_SAD, {-0.55f, -0.35f}},
    {NB_EXPR_ANGRY, {-0.55f, 0.45f}},
};
#define NB_AFFECT_ANCHOR_COUNT (sizeof(kAnchors) / sizeof(kAnchors[0]))

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float distance(nb_affect_vec_t a, nb_affect_vec_t b) {
    const float dv = a.valence - b.valence;
    const float da = a.arousal - b.arousal;
    return sqrtf(dv * dv + da * da);
}

static uint32_t hash_key(const char *key) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (const char *c = key; *c != '\0'; c++) {
        h ^= (uint8_t)*c;
        h *= 16777619u;
    }
    return h;
}

nb_affect_personality_t nb_affect_default_personality(void) {
    nb_affect_personality_t p = {{0.10f, 0.05f}, 1.0f, 60000u, 2.0f};
    return p;
}

void nb_affect_init(nb_affect_t *affect, const nb_affect_personality_t *personality) {
    if (affect == NULL) {
        return;
    }
    memset(affect, 0, sizeof(*affect));
    affect->personality = personality != NULL ? *personality : nb_affect_default_personality();
    affect->mood = affect->personality.temperament;
    affect->expression = NB_EXPR_NEUTRAL;
}

// The habit slot for a key, starting a new window when the last one is over.
// A full table recycles the slot whose window started longest ago.
static nb_affect_habit_t *habit_for(nb_affect_t *affect, uint32_t key_hash, uint32_t now_ms) {
    nb_affect_habit_t *slot = NULL;
    for (size_t i = 0; i < NB_AFFECT_HABIT_SLOTS && slot == NULL; i++) {
        if (affect->habits[i].used && affect->habits[i].key_hash == key_hash) {
            slot = &affect->habits[i];
        }
    }
    if (slot != NULL && now_ms - slot->window_start_ms < NB_AFFECT_HABIT_WINDOW_MS) {
        return slot;
    }
    for (size_t i = 0; i < NB_AFFECT_HABIT_SLOTS && slot == NULL; i++) {
        if (!affect->habits[i].used) {
            slot = &affect->habits[i];
        }
    }
    if (slot == NULL) {
        slot = &affect->habits[0];
        for (size_t i = 1; i < NB_AFFECT_HABIT_SLOTS; i++) {
            nb_affect_habit_t *h = &affect->habits[i];
            if (now_ms - h->window_start_ms > now_ms - slot->window_start_ms) {
                slot = h;
            }
        }
    }
    slot->used = true;
    slot->key_hash = key_hash;
    slot->window_start_ms = now_ms;
    slot->repeats = 0;
    slot->applied = 0.0f;
    return slot;
}

float nb_affect_stimulate(nb_affect_t *affect, const nb_affect_stimulus_t *stimulus,
                          uint32_t now_ms) {
    if (affect == NULL || stimulus == NULL || stimulus->key == NULL || stimulus->cap <= 0.0f) {
        return 0.0f;
    }
    nb_affect_habit_t *habit = habit_for(affect, hash_key(stimulus->key), now_ms);
    const float ratio = stimulus->habituation > 0.0f && stimulus->habituation <= 1.0f
                            ? stimulus->habituation
                            : NB_AFFECT_DEFAULT_HABITUATION;
    const float habituation = powf(ratio, (float)habit->repeats);
    const float gain = affect->personality.reactivity * habituation;
    const float dv = stimulus->valence * gain;
    const float da = stimulus->arousal * gain;
    const float magnitude = fabsf(dv) + fabsf(da);
    if (habit->repeats < NB_AFFECT_MAX_REPEATS) {
        habit->repeats++;
    }
    const float remaining = stimulus->cap - habit->applied;
    if (magnitude <= 0.0f || remaining <= 0.0f) {
        return 0.0f;
    }
    const float scale = magnitude > remaining ? remaining / magnitude : 1.0f;
    affect->mood.valence = clampf(affect->mood.valence + dv * scale, -1.0f, 1.0f);
    affect->mood.arousal = clampf(affect->mood.arousal + da * scale, -1.0f, 1.0f);
    habit->applied += magnitude * scale;
    return habituation * scale;
}

void nb_affect_set_bias(nb_affect_t *affect, float valence, float arousal) {
    if (affect == NULL) {
        return;
    }
    affect->bias.valence = clampf(valence, -NB_AFFECT_BIAS_LIMIT, NB_AFFECT_BIAS_LIMIT);
    affect->bias.arousal = clampf(arousal, -NB_AFFECT_BIAS_LIMIT, NB_AFFECT_BIAS_LIMIT);
}

void nb_affect_tick(nb_affect_t *affect, uint32_t dt_ms) {
    if (affect == NULL || dt_ms == 0u) {
        return;
    }
    const nb_affect_personality_t *p = &affect->personality;
    const float recovery_s = (float)(p->recovery_ms == 0u ? 1u : p->recovery_ms) / 1000.0f;
    const float tau_s = recovery_s / NB_AFFECT_LN20;
    const float slowdown = p->negative_slowdown < 1.0f ? 1.0f : p->negative_slowdown;
    const float valence_tau = affect->mood.valence < 0.0f ? tau_s * slowdown : tau_s;
    const float dt_s = (float)dt_ms / 1000.0f;

    const float target_v = clampf(p->temperament.valence + affect->bias.valence, -1.0f, 1.0f);
    const float target_a = clampf(p->temperament.arousal + affect->bias.arousal, -1.0f, 1.0f);
    affect->mood.valence = target_v + (affect->mood.valence - target_v) * expf(-dt_s / valence_tau);
    affect->mood.arousal = target_a + (affect->mood.arousal - target_a) * expf(-dt_s / tau_s);
}

static const anchor_t *anchor_of(nb_expression_t expression) {
    for (size_t i = 0; i < NB_AFFECT_ANCHOR_COUNT; i++) {
        if (kAnchors[i].expression == expression) {
            return &kAnchors[i];
        }
    }
    return NULL;
}

nb_expression_t nb_affect_resolve(nb_affect_t *affect) {
    if (affect == NULL) {
        return NB_EXPR_NEUTRAL;
    }
    const float from_rest = distance(affect->mood, affect->personality.temperament);
    const anchor_t *current = anchor_of(affect->expression);
    const float threshold = current == NULL ? NB_AFFECT_SHOW_DISTANCE : NB_AFFECT_HIDE_DISTANCE;
    if (from_rest < threshold) {
        affect->expression = NB_EXPR_NEUTRAL;
        return affect->expression;
    }

    const anchor_t *nearest = &kAnchors[0];
    for (size_t i = 1; i < NB_AFFECT_ANCHOR_COUNT; i++) {
        if (distance(affect->mood, kAnchors[i].at) < distance(affect->mood, nearest->at)) {
            nearest = &kAnchors[i];
        }
    }
    if (current == NULL || distance(affect->mood, nearest->at) + NB_AFFECT_SWITCH_MARGIN <
                               distance(affect->mood, current->at)) {
        affect->expression = nearest->expression;
    }
    return affect->expression;
}
