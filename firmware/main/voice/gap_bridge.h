#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/nb_gap_bridge.h"

// Hides short pauses in the person's speech from the server (core/nb_gap_bridge)
// by holding back the frames the voice detector calls silence. The server ends
// a turn on its own silence timer, which a breath between two phrases is
// enough to trip. Process runs in the AFE task, on frames that go to the uplink
// while the robot listens.
class GapBridge {
public:
    using Sink = std::function<void(std::vector<int16_t>&&)>;

    GapBridge(int frame_ms, uint32_t max_hold_ms, uint32_t preroll_ms);

    // At each new capture session: a new turn.
    void Reset();
    void Process(std::vector<int16_t>&& frame, bool speech, const Sink& sink);

private:
    nb_gap_bridge_t bridge_ = {};
    std::deque<std::vector<int16_t>> kept_;
    size_t keep_frames_ = 0;
    uint32_t frame_ms_ = 0;
    uint32_t held_frames_ = 0;  // frames held in the current pause, kept or not
};
