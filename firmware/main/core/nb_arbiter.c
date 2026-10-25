#include "nb_arbiter.h"

#include <stddef.h>
#include <string.h>

void nb_arbiter_init(nb_arbiter_t *arbiter, nb_expression_t baseline) {
    memset(arbiter, 0, sizeof(*arbiter));
    arbiter->baseline = baseline;
}

bool nb_arbiter_propose(nb_arbiter_t *arbiter, nb_source_t source,
                        const nb_proposal_t *proposal, uint32_t now_ms) {
    if ((int)source < 0 || source >= NB_SOURCE_COUNT || proposal == NULL ||
        (int)proposal->expression < 0 || proposal->expression >= NB_EXPR_COUNT) {
        return false;
    }
    nb_slot_t *slot = &arbiter->slots[source];
    slot->active = true;
    slot->proposal = *proposal;
    slot->expires_ms = now_ms + proposal->ttl_ms;
    slot->seq = arbiter->next_seq++;
    return true;
}

void nb_arbiter_withdraw(nb_arbiter_t *arbiter, nb_source_t source) {
    if ((int)source >= 0 && source < NB_SOURCE_COUNT) {
        arbiter->slots[source].active = false;
    }
}

void nb_arbiter_enter_idle(nb_arbiter_t *arbiter) {
    for (size_t i = 0; i < NB_SOURCE_COUNT; i++) {
        if (arbiter->slots[i].proposal.transient) {
            arbiter->slots[i].active = false;
        }
    }
}

// Wrap-safe: true once now_ms has reached expires_ms, across the uint32 rollover.
static bool expired(const nb_slot_t *slot, uint32_t now_ms) {
    return slot->proposal.ttl_ms != 0 && (int32_t)(now_ms - slot->expires_ms) >= 0;
}

nb_expression_t nb_arbiter_resolve(nb_arbiter_t *arbiter, uint32_t now_ms) {
    const nb_slot_t *winner = NULL;
    for (size_t i = 0; i < NB_SOURCE_COUNT; i++) {
        nb_slot_t *slot = &arbiter->slots[i];
        if (!slot->active) {
            continue;
        }
        if (expired(slot, now_ms)) {
            slot->active = false;
            continue;
        }
        if (winner == NULL || slot->proposal.priority > winner->proposal.priority ||
            (slot->proposal.priority == winner->proposal.priority &&
             (int32_t)(slot->seq - winner->seq) > 0)) {
            winner = slot;
        }
    }
    return winner != NULL ? winner->proposal.expression : arbiter->baseline;
}
