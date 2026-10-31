#include "voice/barge_in_filter.h"

#include <esp_log.h>

#include "voice/audio_policy.h"

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
        .min_level_dbfs = BARGE_IN_MIN_LEVEL_DBFS,
        .hold_level_dbfs = BARGE_IN_HOLD_LEVEL_DBFS,
    };
    nb_barge_gate_init(&gate_, &cfg);
    // Enough to cover the speech that opened the gate plus the detector's
    // own delay before it called it speech.
    keep_frames_ = NB_BARGE_PREROLL_FRAMES(frame_ms);
}

// The user's own voice level while the robot listens, the reference for
// BARGE_IN_MIN_LEVEL_DBFS: only runs long enough to open the gate count.
void BargeInFilter::LogListeningLevel(bool speech, int level) {
    if (speech) {
        listen_sum_ += level;
        listen_frames_++;
        return;
    }
    if (listen_frames_ >= gate_.cfg.min_speech_frames) {
        ESP_LOGI(TAG, "Listening: %u ms of speech, avg %d dBFS (interrupting needs %d)",
                 (unsigned)(listen_frames_ * frame_ms_), listen_sum_ / (int)listen_frames_,
                 BARGE_IN_MIN_LEVEL_DBFS);
    }
    listen_sum_ = 0;
    listen_frames_ = 0;
}

void BargeInFilter::Reset() {
    nb_barge_gate_arm(&gate_, armed_request_.load());
    kept_.clear();
    kept_voice_frames_ = 0;
    loudest_held_ = NB_BARGE_LEVEL_SILENCE;
    listen_sum_ = 0;
    listen_frames_ = 0;
}

void BargeInFilter::Flush(const Sink& sink) {
    while (!kept_.empty()) {
        sink(std::move(kept_.front().pcm));
        kept_.pop_front();
    }
    kept_voice_frames_ = 0;
}

void BargeInFilter::Process(std::vector<int16_t>&& frame, bool speech, const Sink& sink) {
    const bool armed = armed_request_.load();
    if (armed != gate_.armed) {
        if (!armed && loudest_held_ > NB_BARGE_LEVEL_SILENCE) {
            ESP_LOGI(TAG, "Held back speech below %d dBFS while speaking (loudest run avg %d)",
                     BARGE_IN_MIN_LEVEL_DBFS, loudest_held_);
        }
        // A user can start just before playback drains, too late to satisfy
        // MIN_SPEECH while armed. Preserve that onset when entering listening;
        // don't replay a buffer that contains only silence/weak residual echo.
        if (!armed && kept_voice_frames_ > 0) Flush(sink);
        Reset();
    }
    const int level = nb_barge_level_dbfs(frame.data(), frame.size());
    if (!gate_.armed) {
        LogListeningLevel(speech, level);
        sink(std::move(frame));
        return;
    }
    switch (nb_barge_gate_step(&gate_, speech, level)) {
    case NB_BARGE_PASS:
        sink(std::move(frame));
        break;
    case NB_BARGE_HOLD:
        loudest_held_ = std::max(loudest_held_, nb_barge_gate_run_average(&gate_));
        kept_.push_back({std::move(frame), speech && level >= BARGE_IN_MIN_LEVEL_DBFS});
        if (kept_.back().voice) ++kept_voice_frames_;
        if (kept_.size() > keep_frames_) {
            if (kept_.front().voice) --kept_voice_frames_;
            kept_.pop_front();
        }
        break;
    case NB_BARGE_OPENED: {
        // The core cleared the run on opening; recompute from its levels.
        int sum = 0;
        for (uint8_t i = 0; i < gate_.cfg.min_speech_frames; i++) {
            sum += gate_.levels[(gate_.head + NB_BARGE_MAX_RUN - 1 - i) % NB_BARGE_MAX_RUN];
        }
        ESP_LOGI(TAG, "Voice while speaking (run avg %d dBFS): sending %u ms to interrupt",
                 sum / gate_.cfg.min_speech_frames, (unsigned)((kept_.size() + 1) * frame_ms_));
        Flush(sink);
        sink(std::move(frame));
        break;
    }
    }
}
