#include "wake/micro_wake_word.h"

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <tensorflow/lite/experimental/microfrontend/lib/frontend_util.h>
#include <tensorflow/lite/micro/micro_allocator.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/micro/micro_resource_variable.h>
#include <tensorflow/lite/schema/schema_generated.h>

namespace {

constexpr const char* TAG = "MicroWakeWord";

extern "C" const uint8_t kModelStart[] asm("_binary_nexo_tflite_start");

// Frontend settings microWakeWord trains with (pymicro-features).
constexpr int kSampleRate = 16000;
constexpr int kFeatureSize = 40;

// Detection tuning for the current Nexo model. Its ROC on synthetic voices
// shows no misses at any cutoff below 1.0 and one false accept in ~5 h of
// party noise at 0.73, so 0.90 leaves margin both ways until real recordings
// say otherwise. Near misses above 0.40 are logged to tune it.
constexpr nb_wake_detect_config_t kDetectConfig = {
    .cutoff = 229,        // 0.90
    .window = 5,          // 150 ms of probabilities
    .warmup_steps = 34,   // ~1 s after Start
    .report_floor = 102,  // 0.40
};

// The model needs ~ tens of KB; the arena lives in PSRAM because internal RAM
// is scarce and one inference every 30 ms is light. The used size is logged.
constexpr size_t kArenaSize = 64 * 1024;
constexpr int kResourceVariables = 20;

// Every op the microWakeWord architectures use, so a retrained model with a
// different layout still loads.
using OpResolver = tflite::MicroMutableOpResolver<20>;

bool RegisterOps(OpResolver& r) {
    return r.AddCallOnce() == kTfLiteOk && r.AddVarHandle() == kTfLiteOk &&
           r.AddReadVariable() == kTfLiteOk && r.AddAssignVariable() == kTfLiteOk &&
           r.AddConv2D() == kTfLiteOk && r.AddDepthwiseConv2D() == kTfLiteOk &&
           r.AddFullyConnected() == kTfLiteOk && r.AddLogistic() == kTfLiteOk &&
           r.AddQuantize() == kTfLiteOk && r.AddReshape() == kTfLiteOk &&
           r.AddSplitV() == kTfLiteOk && r.AddStridedSlice() == kTfLiteOk &&
           r.AddConcatenation() == kTfLiteOk && r.AddMul() == kTfLiteOk &&
           r.AddAdd() == kTfLiteOk && r.AddMean() == kTfLiteOk &&
           r.AddAveragePool2D() == kTfLiteOk && r.AddMaxPool2D() == kTfLiteOk &&
           r.AddPad() == kTfLiteOk && r.AddPack() == kTfLiteOk;
}

FrontendConfig MakeFrontendConfig() {
    FrontendConfig c;
    FrontendFillConfigWithDefaults(&c);
    c.window.size_ms = 30;
    c.window.step_size_ms = 10;
    c.filterbank.num_channels = kFeatureSize;
    c.filterbank.lower_band_limit = 125.0f;
    c.filterbank.upper_band_limit = 7500.0f;
    c.noise_reduction.smoothing_bits = 10;
    c.noise_reduction.even_smoothing = 0.025f;
    c.noise_reduction.odd_smoothing = 0.06f;
    c.noise_reduction.min_signal_remaining = 0.05f;
    c.pcan_gain_control.enable_pcan = 1;
    c.pcan_gain_control.strength = 0.95f;
    c.pcan_gain_control.offset = 80.0f;
    c.pcan_gain_control.gain_bits = 21;
    c.log_scale.enable_log = 1;
    c.log_scale.scale_shift = 6;
    return c;
}

}  // namespace

MicroWakeWord::MicroWakeWord() = default;

MicroWakeWord::~MicroWakeWord() {
    interpreter_.reset();
    resolver_.reset();
    if (arena_ != nullptr) {
        heap_caps_free(arena_);
    }
    if (frontend_ready_) {
        FrontendFreeStateContents(&frontend_);
    }
}

bool MicroWakeWord::Initialize() {
    const tflite::Model* model = tflite::GetModel(kModelStart);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %lu, expected %d", (unsigned long)model->version(),
                 TFLITE_SCHEMA_VERSION);
        return false;
    }

    arena_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, kArenaSize, MALLOC_CAP_SPIRAM));
    if (arena_ == nullptr) {
        ESP_LOGE(TAG, "No PSRAM for the %u-byte tensor arena", (unsigned)kArenaSize);
        return false;
    }

    auto resolver = std::make_unique<OpResolver>();
    if (!RegisterOps(*resolver)) {
        ESP_LOGE(TAG, "Failed to register the model ops");
        return false;
    }
    resolver_ = std::move(resolver);

    // Resource variables hold the streaming state between inferences; they
    // are allocated from the same arena as the tensors.
    tflite::MicroAllocator* allocator = tflite::MicroAllocator::Create(arena_, kArenaSize);
    tflite::MicroResourceVariables* variables =
        tflite::MicroResourceVariables::Create(allocator, kResourceVariables);
    interpreter_ = std::make_unique<tflite::MicroInterpreter>(model, *resolver_, allocator, variables);
    if (interpreter_->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed (arena %u bytes)", (unsigned)kArenaSize);
        interpreter_.reset();
        return false;
    }

    TfLiteTensor* in = interpreter_->input(0);
    TfLiteTensor* out = interpreter_->output(0);
    if (in->type != kTfLiteInt8 || in->dims->size != 3 || in->dims->data[2] != kFeatureSize ||
        out->type != kTfLiteUInt8) {
        ESP_LOGE(TAG, "Unexpected model I/O: input type %d dims %d, output type %d", in->type,
                 in->dims->size, out->type);
        interpreter_.reset();
        return false;
    }
    input_ = in->data.int8;
    input_frames_ = static_cast<size_t>(in->dims->data[1]);
    input_scale_ = in->params.scale;
    input_zero_point_ = in->params.zero_point;

    FrontendConfig config = MakeFrontendConfig();
    if (!FrontendPopulateState(&config, &frontend_, kSampleRate)) {
        ESP_LOGE(TAG, "Failed to set up the audio frontend");
        interpreter_.reset();
        return false;
    }
    frontend_ready_ = true;
    nb_wake_detect_init(&detect_, &kDetectConfig);

    ESP_LOGI(TAG, "Ready: \"%s\", arena %u/%u bytes, %u frames per inference, cutoff %u/255",
             wake_word_.c_str(), (unsigned)interpreter_->arena_used_bytes(), (unsigned)kArenaSize,
             (unsigned)input_frames_, (unsigned)kDetectConfig.cutoff);
    return true;
}

void MicroWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    callback_ = std::move(callback);
}

void MicroWakeWord::ResetLocked() {
    FrontendReset(&frontend_);
    interpreter_->Reset();
    frames_ = 0;
    nb_wake_detect_reset(&detect_);
}

void MicroWakeWord::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (interpreter_ == nullptr) {
        return;
    }
    ResetLocked();
    running_ = true;
}

void MicroWakeWord::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

void MicroWakeWord::FeedMono(const int16_t* samples, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    while (running_ && count > 0) {
        size_t read = 0;
        FrontendOutput frame = FrontendProcessSamples(&frontend_, samples, count, &read);
        samples += read;
        count -= read;
        if (frame.size == static_cast<size_t>(kFeatureSize)) {
            PushFrame(frame.values);
        }
    }
}

void MicroWakeWord::PushFrame(const uint16_t* values) {
    int8_t* dst = input_ + frames_ * kFeatureSize;
    for (int i = 0; i < kFeatureSize; i++) {
        dst[i] = nb_wake_quantize_feature(values[i], input_scale_, input_zero_point_);
    }
    if (++frames_ < input_frames_) {
        return;
    }
    frames_ = 0;
    if (interpreter_->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed");
        return;
    }

    uint8_t peak = 0;
    const uint8_t probability = interpreter_->output(0)->data.uint8[0];
    switch (nb_wake_detect_step(&detect_, probability, &peak)) {
    case NB_WAKE_DETECTED:
        ESP_LOGI(TAG, "Detected \"%s\" (average %u/255)", wake_word_.c_str(), peak);
        running_ = false;
        if (callback_) {
            callback_(wake_word_);
        }
        break;
    case NB_WAKE_NEAR_MISS:
        ESP_LOGI(TAG, "Near miss: peak %u/255, cutoff %u", peak, (unsigned)kDetectConfig.cutoff);
        break;
    case NB_WAKE_NONE:
        break;
    }
}
