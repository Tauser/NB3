#ifndef NB3_EXPRESSION_ARBITER_H
#define NB3_EXPRESSION_ARBITER_H

#include <cstdint>
#include <mutex>

#include "core/nb_arbiter.h"

// Priorities of every expression source, in one place so their order is
// visible: a reflex beats a touch, which beats an AI command, and so on.
// The mood is below everything else: it is what the face shows at rest.
namespace priority {
constexpr uint8_t kMood = 3;
constexpr uint8_t kState = 5;
constexpr uint8_t kEmotion = 20;
constexpr uint8_t kCommand = 30;
constexpr uint8_t kTouch = 40;
constexpr uint8_t kReflex = 50;
constexpr uint8_t kFault = 60;
}  // namespace priority

// Thread-safe shell over nb_arbiter. Proposals arrive from the xiaozhi task
// (emotion, state, MCP) and are resolved by the face on the LVGL task.
class ExpressionArbiter {
public:
    explicit ExpressionArbiter(nb_expression_t baseline);

    void Propose(nb_source_t source, const nb_proposal_t& proposal);
    void Withdraw(nb_source_t source);
    void EnterIdle();
    // winner, if given, gets the source that won (NB_SOURCE_COUNT: baseline).
    nb_expression_t Resolve(nb_source_t* winner = nullptr);

private:
    static uint32_t NowMs();

    std::mutex mutex_;
    nb_arbiter_t arbiter_;
};

#endif  // NB3_EXPRESSION_ARBITER_H
