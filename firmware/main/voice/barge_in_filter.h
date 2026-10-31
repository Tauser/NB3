#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/nb_barge_gate.h"

// Holds the mic uplink back while the robot speaks until the voice detector
// has heard real speech, loud enough to be someone close to the robot
// (nb_barge_gate), so noise, residual echo and voices across the room do not
// interrupt the reply. Frames kept while closed are sent first when it opens, so the
// server hears the words from their start. Process runs in the AFE task;
// SetArmed may be called from any task.
class BargeInFilter {
public:
    using Sink = std::function<void(std::vector<int16_t>&&)>;

    explicit BargeInFilter(int frame_ms);

    void SetArmed(bool armed) { armed_request_.store(armed); }
    // Called by the processing owner at a new capture session, never by the
    // control task. Old preroll must not leak into a later conversation.
    void Reset();
    void Process(std::vector<int16_t>&& frame, bool speech, const Sink& sink);

private:
    void LogListeningLevel(bool speech, int level);
    void Flush(const Sink& sink);
    struct HeldFrame {
        std::vector<int16_t> pcm;
        bool voice;
    };

    nb_barge_gate_t gate_ = {};
    std::atomic<bool> armed_request_{false};
    std::deque<HeldFrame> kept_;
    size_t kept_voice_frames_ = 0;
    size_t keep_frames_ = 0;
    int frame_ms_ = 0;
    int loudest_held_ = NB_BARGE_LEVEL_SILENCE;  // best run average that stayed below, this turn
    int listen_sum_ = 0;          // dBFS sum of the current speech run while listening
    uint32_t listen_frames_ = 0;
};
