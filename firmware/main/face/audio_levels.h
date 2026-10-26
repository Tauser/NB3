#ifndef NB3_AUDIO_LEVELS_H
#define NB3_AUDIO_LEVELS_H

#include <atomic>
#include <cmath>
#include <cstdint>

// How loud the microphone and the speaker are, 0..1. The audio codec writes
// them from its tasks; the face reads them on the LVGL task to drive the
// listening bars and the speech bubble. A level older than kStaleMs reads as
// silence, so a stream that stops does not freeze the bars mid-word.
class AudioLevels {
public:
    void SetMic(float level, uint32_t now_ms) { Set(mic_, mic_ms_, level, now_ms); }
    void SetSpeaker(float level, uint32_t now_ms) { Set(speaker_, speaker_ms_, level, now_ms); }
    float Mic(uint32_t now_ms) const { return Get(mic_, mic_ms_, now_ms); }
    float Speaker(uint32_t now_ms) const { return Get(speaker_, speaker_ms_, now_ms); }

    // RMS of a block of samples mapped to 0..1: -45 dBFS and below is
    // silence, -12 dBFS and above is full scale. Starting values, tuned on the
    // bench.
    static float FromSamples(const int16_t* samples, int count) {
        if (count <= 0) {
            return 0.0f;
        }
        double sum = 0.0;
        for (int i = 0; i < count; i++) {
            sum += static_cast<double>(samples[i]) * samples[i];
        }
        const double rms = std::sqrt(sum / count) / 32768.0;
        const double db = 20.0 * std::log10(rms + 1e-9);
        const double level = (db + 45.0) / 33.0;
        return static_cast<float>(level < 0.0 ? 0.0 : (level > 1.0 ? 1.0 : level));
    }

private:
    static constexpr uint32_t kStaleMs = 200;

    static void Set(std::atomic<float>& level, std::atomic<uint32_t>& at, float value,
                    uint32_t now_ms) {
        level.store(value, std::memory_order_relaxed);
        at.store(now_ms, std::memory_order_relaxed);
    }
    static float Get(const std::atomic<float>& level, const std::atomic<uint32_t>& at,
                     uint32_t now_ms) {
        return now_ms - at.load(std::memory_order_relaxed) > kStaleMs
                   ? 0.0f
                   : level.load(std::memory_order_relaxed);
    }

    std::atomic<float> mic_{0.0f};
    std::atomic<float> speaker_{0.0f};
    std::atomic<uint32_t> mic_ms_{0};
    std::atomic<uint32_t> speaker_ms_{0};
};

#endif  // NB3_AUDIO_LEVELS_H
