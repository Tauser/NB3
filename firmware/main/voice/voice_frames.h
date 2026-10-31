#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Carries VAD tags across AFE/Opus frame boundaries together with the PCM.
// A frame is tagged speech if any of its segments was tagged speech by AFE;
// this is a frame-level decision, not a claim that every sample is speech.
class VoiceFrames {
public:
    explicit VoiceFrames(size_t samples) : capacity_(samples) { pcm_.reserve(samples); }

    void Reset() { pcm_.clear(); speech_ = false; }

    template <typename Sink>
    void Push(const int16_t* data, size_t count, bool speech, const Sink& sink) {
        if (capacity_ == 0) return;
        while (count > 0) {
            const size_t take = std::min(count, capacity_ - pcm_.size());
            pcm_.insert(pcm_.end(), data, data + take);
            speech_ = speech_ || speech;
            data += take;
            count -= take;
            if (pcm_.size() == capacity_) {
                const bool frame_speech = speech_;
                speech_ = false;
                sink(std::move(pcm_), frame_speech);
                pcm_.clear();
                pcm_.reserve(capacity_);
            }
        }
    }

private:
    const size_t capacity_;
    std::vector<int16_t> pcm_;
    bool speech_ = false;
};
