#include "voice/barge_in_filter.h"

#include <esp_log.h>

#include "board/config.h"

namespace {
constexpr const char* TAG = "BargeIn";

uint8_t FramesFor(int ms, int frame_ms) {
    const int frames = (ms + frame_ms - 1) / frame_ms;
    return static_cast<uint8_t>(frames < 1 ? 1 : (frames > 255 ? 255 : frames));
}
}  // namespace

BargeInFilter::BargeInFilter(int frame_ms) : frame_ms_(frame_ms) {
    const nb_barge_gate_config_t cfg = {
        .min_speech_frames = FramesFor(BARGE_IN_MIN_SPEECH_MS, frame_ms),
        .hold_frames = FramesFor(BARGE_IN_HOLD_MS, frame_ms),
    };
    nb_barge_gate_init(&gate_, &cfg);
    // Enough to cover the speech that opened the gate plus the detector's
    // own delay before it called it speech.
    keep_frames_ = cfg.min_speech_frames + FramesFor(BARGE_IN_EXTRA_PREROLL_MS, frame_ms);
}

void BargeInFilter::Process(std::vector<int16_t>&& frame, bool speech, const Sink& sink) {
    const bool armed = armed_request_.load();
    if (armed != gate_.armed) {
        if (!armed && speech_frames_held_ > 0) {
            ESP_LOGI(TAG, "Held back %u ms of voice-like sound while speaking",
                     (unsigned)(speech_frames_held_ * frame_ms_));
        }
        nb_barge_gate_arm(&gate_, armed);
        kept_.clear();
        speech_frames_held_ = 0;
    }

    switch (nb_barge_gate_step(&gate_, speech)) {
    case NB_BARGE_PASS:
        sink(std::move(frame));
        break;
    case NB_BARGE_HOLD:
        if (speech) {
            speech_frames_held_++;
        }
        kept_.push_back(std::move(frame));
        if (kept_.size() > keep_frames_) {
            kept_.pop_front();
        }
        break;
    case NB_BARGE_OPENED:
        ESP_LOGI(TAG, "Voice while speaking: sending %u ms of audio to interrupt",
                 (unsigned)((kept_.size() + 1) * frame_ms_));
        while (!kept_.empty()) {
            sink(std::move(kept_.front()));
            kept_.pop_front();
        }
        sink(std::move(frame));
        speech_frames_held_ = 0;
        break;
    }
}
