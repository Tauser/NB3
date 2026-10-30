#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/nb_barge_gate.h"

// Holds the mic uplink back while the robot speaks until the voice detector
// has heard real speech (nb_barge_gate), so noise does not interrupt the
// reply. Frames kept while closed are sent first when it opens, so the
// server hears the words from their start. Process runs in the AFE task;
// SetArmed may be called from any task.
class BargeInFilter {
public:
    using Sink = std::function<void(std::vector<int16_t>&&)>;

    explicit BargeInFilter(int frame_ms);

    void SetArmed(bool armed) { armed_request_.store(armed); }
    void Process(std::vector<int16_t>&& frame, bool speech, const Sink& sink);

private:
    nb_barge_gate_t gate_ = {};
    std::atomic<bool> armed_request_{false};
    std::deque<std::vector<int16_t>> kept_;
    size_t keep_frames_ = 0;
    int frame_ms_ = 0;
    uint32_t speech_frames_held_ = 0;  // speech-flagged frames dropped this turn
};
