#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <tensorflow/lite/experimental/microfrontend/lib/frontend.h>

#include "core/nb_wake_detect.h"

namespace tflite {
class MicroInterpreter;
class MicroOpResolver;
}  // namespace tflite

// "Nexo" wake word: a microWakeWord streaming model on TFLite Micro. Fed
// with the AFE's processed 16 kHz mono output; every 10 ms the microfrontend
// makes one 40-band feature frame, every 3 frames the model gives one
// probability, and nb_wake_detect decides. Runs in the caller's task (the
// AFE processing task), like xiaozhi's MultiNet wake word.
class MicroWakeWord {
public:
    MicroWakeWord();
    ~MicroWakeWord();
    MicroWakeWord(const MicroWakeWord&) = delete;
    MicroWakeWord& operator=(const MicroWakeWord&) = delete;

    bool Initialize();
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    // Start clears the audio and model state left from the last session.
    void Start();
    void Stop();
    void FeedMono(const int16_t* samples, size_t count);
    // Text of the sentence the robot starts saying. A sentence with the wake
    // word in it ("anexo" included) mutes detection until a later sentence
    // without it has played for a moment, so the robot does not wake itself.
    void OnSpokenText(const std::string& text);
    const std::string& wake_word() const { return wake_word_; }

private:
    void ResetLocked();
    void PushFrame(const uint16_t* values);

    std::string wake_word_ = "Nexo";
    std::function<void(const std::string&)> callback_;
    std::mutex mutex_;
    bool running_ = false;
    bool own_voice_ = false;            // the current sentence says the wake word
    int64_t own_voice_until_us_ = 0;    // its audio may still be playing until then

    FrontendState frontend_ = {};
    bool frontend_ready_ = false;
    nb_wake_detect_t detect_ = {};

    uint8_t* arena_ = nullptr;
    std::unique_ptr<tflite::MicroOpResolver> resolver_;
    std::unique_ptr<tflite::MicroInterpreter> interpreter_;
    int8_t* input_ = nullptr;
    size_t input_frames_ = 0;  // feature frames per inference (the model's stride)
    size_t frames_ = 0;        // frames buffered for the next inference
    float input_scale_ = 0.0f;
    int input_zero_point_ = 0;
};
