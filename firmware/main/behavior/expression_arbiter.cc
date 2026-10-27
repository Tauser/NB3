#include "expression_arbiter.h"

#include <esp_timer.h>

ExpressionArbiter::ExpressionArbiter(nb_expression_t baseline) {
    nb_arbiter_init(&arbiter_, baseline);
}

uint32_t ExpressionArbiter::NowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void ExpressionArbiter::Propose(nb_source_t source, const nb_proposal_t& proposal) {
    std::lock_guard<std::mutex> lock(mutex_);
    nb_arbiter_propose(&arbiter_, source, &proposal, NowMs());
}

void ExpressionArbiter::Withdraw(nb_source_t source) {
    std::lock_guard<std::mutex> lock(mutex_);
    nb_arbiter_withdraw(&arbiter_, source);
}

void ExpressionArbiter::EnterIdle() {
    std::lock_guard<std::mutex> lock(mutex_);
    nb_arbiter_enter_idle(&arbiter_);
}

nb_expression_t ExpressionArbiter::Resolve(nb_source_t* winner) {
    std::lock_guard<std::mutex> lock(mutex_);
    return nb_arbiter_resolve_source(&arbiter_, NowMs(), winner);
}
