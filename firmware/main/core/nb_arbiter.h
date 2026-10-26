#ifndef NB_ARBITER_H
#define NB_ARBITER_H

#include <stdbool.h>
#include <stdint.h>

#include "nb_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

// Who is proposing. Each source owns one slot: a new proposal from the same
// source replaces its previous one.
typedef enum {
    NB_SOURCE_STATE = 0,  // device state (listening, speaking, ...)
    NB_SOURCE_EMOTION,    // emotion sent by the cloud
    NB_SOURCE_COMMAND,    // explicit request from the AI through an MCP tool
    NB_SOURCE_TOUCH,      // body touch reactions
    NB_SOURCE_REFLEX,     // short local reflexes (startle, blink bursts)
    NB_SOURCE_MOOD,       // the robot's own mood, shown when nothing else is
    NB_SOURCE_COUNT
} nb_source_t;

typedef struct {
    nb_expression_t expression;
    uint8_t priority;     // higher wins; ties go to the most recent proposal
    uint32_t ttl_ms;      // 0 = holds until replaced or withdrawn
    bool transient;       // cleared when the device returns to IDLE
} nb_proposal_t;

typedef struct {
    bool active;
    nb_proposal_t proposal;
    uint32_t expires_ms;
    uint32_t seq;
} nb_slot_t;

// The arbiter is the only place that decides what the face shows. Modifiers
// propose; nothing else writes the expression. With no live proposal the
// baseline expression wins.
typedef struct {
    nb_expression_t baseline;
    nb_slot_t slots[NB_SOURCE_COUNT];
    uint32_t next_seq;
} nb_arbiter_t;

void nb_arbiter_init(nb_arbiter_t *arbiter, nb_expression_t baseline);

// Replaces the source's slot. Returns false for an invalid source/expression.
bool nb_arbiter_propose(nb_arbiter_t *arbiter, nb_source_t source,
                        const nb_proposal_t *proposal, uint32_t now_ms);

void nb_arbiter_withdraw(nb_arbiter_t *arbiter, nb_source_t source);

// IDLE is the baseline state: everything transient is dropped so a stale
// reaction cannot leak into the next interaction.
void nb_arbiter_enter_idle(nb_arbiter_t *arbiter);

// Drops expired proposals and returns the winning expression.
nb_expression_t nb_arbiter_resolve(nb_arbiter_t *arbiter, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // NB_ARBITER_H
