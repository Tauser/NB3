#include "voice/gap_bridge.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "GapBridge";
}  // namespace

GapBridge::GapBridge(int frame_ms, uint32_t max_hold_ms, uint32_t preroll_ms)
    : frame_ms_(static_cast<uint32_t>(frame_ms)) {
    const nb_gap_bridge_config_t cfg = {max_hold_ms, preroll_ms};
    nb_gap_bridge_init(&bridge_, &cfg);
    keep_frames_ = nb_gap_bridge_preroll_frames(&bridge_, frame_ms_);
}

void GapBridge::Reset() {
    nb_gap_bridge_reset(&bridge_);
    kept_.clear();
    held_frames_ = 0;
}

void GapBridge::Process(std::vector<int16_t>&& frame, bool speech, const Sink& sink) {
    switch (nb_gap_bridge_step(&bridge_, speech, frame_ms_)) {
    case NB_GAP_HOLD:
        held_frames_++;
        kept_.push_back(std::move(frame));
        if (kept_.size() > keep_frames_) {
            kept_.pop_front();
        }
        return;
    case NB_GAP_FLUSH_AND_SEND:
        ESP_LOGI(TAG, "Hid a %u ms pause from the server (sent %u ms before the words)",
                 (unsigned)(held_frames_ * frame_ms_), (unsigned)(kept_.size() * frame_ms_));
        while (!kept_.empty()) {
            sink(std::move(kept_.front()));
            kept_.pop_front();
        }
        held_frames_ = 0;
        sink(std::move(frame));
        return;
    case NB_GAP_SEND:
        if (held_frames_ > 0) {
            ESP_LOGI(TAG, "Silence outlasted %u ms: sending it live so the server ends the turn",
                     (unsigned)(held_frames_ * frame_ms_));
        }
        kept_.clear();
        held_frames_ = 0;
        sink(std::move(frame));
        return;
    }
}
