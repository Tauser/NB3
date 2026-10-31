#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "behavior/arc_modifier.h"
#include "behavior/head_behavior.h"
#include "behavior/cpu_report.h"
#include "behavior/link_monitor.h"
#include "behavior/personality_drift.h"
#include "behavior/reminder_service.h"
#include "behavior/sound_bank.h"
#include "mcp/reminder_tools.h"
#include "mcp/head_tools.h"
#include "motion/motion_bench.h"
#include "motion/motion_service.h"
#include "behavior/behavior_modifier.h"
#include "behavior/expression_arbiter.h"
#include "behavior/state_led.h"
#include "behavior/energy_modifier.h"
#include "behavior/initiative_modifier.h"
#include "behavior/mood_lights.h"
#include "behavior/mood_modifier.h"
#include "behavior/touch_modifier.h"
#include "face/audio_levels.h"
#include "face/face_display.h"
#include "mcp/face_tools.h"
#include "mcp/memory_tools.h"
#include "memory/clock_service.h"
#include "memory/memory_service.h"
#include "memory/power_cut_test.h"
#include "memory/store_service.h"
#include "core/nb_hpf.h"
#include "core/nb_spk_ref.h"
#include "core/nb_output_activity.h"
#include "core/nb_personality.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <driver/i2s_std.h>

#define TAG "NoiseBotV3Board"

// Duplex I2S codec for the INMP441: reads the slot selected by the mic's L/R
// pin, high-passes the signal and honours input_gain_ (dB) like the codecs
// with a PGA do. It also reports how loud the mic and the speaker are, for
// the face's listening bars and speech bubble.
//
// With device AEC on, each mic sample goes out paired with what the speaker
// was playing at that moment (input format "MR"), built by core/nb_spk_ref
// from the I2S DMA events: this board has no codec loopback to give the AEC
// its reference.
class NoiseBotV3AudioCodec : public NoAudioCodecDuplex {
public:
    NoiseBotV3AudioCodec(int input_sample_rate, int output_sample_rate,
                         gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                         AudioLevels& levels)
        : NoAudioCodecDuplex(input_sample_rate, output_sample_rate, bclk, ws, dout, din),
          levels_(levels) {
        input_gain_ = AUDIO_INPUT_GAIN_DB;
        nb_hpf_init(&hpf_, static_cast<float>(input_sample_rate), AUDIO_INPUT_HPF_HZ);
        // The base codec always reads the left slot; follow the mic's L/R pin.
        i2s_std_slot_config_t slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
        slot_cfg.slot_mask = AUDIO_I2S_MIC_SLOT;
        ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(rx_handle_, &slot_cfg));
#if CONFIG_USE_DEVICE_AEC
        StartReference();
#endif
    }

    bool IsOutputActive() const override {
        // i2s_channel_write returns when DMA accepts PCM, not when the last
        // sample reaches the amplifier. Cover that bounded queue as well as
        // the tail measured from nonzero samples reported by on_sent.
        const uint32_t dma_ms = (AUDIO_CODEC_DMA_DESC_NUM * AUDIO_CODEC_DMA_FRAME_NUM * 1000u +
                                 output_sample_rate_ - 1) / output_sample_rate_;
        portENTER_CRITICAL(&ref_lock_);
        const bool active = nb_output_activity_active(&output_activity_, NowMs(), dma_ms,
                                                       BARGE_IN_OUTPUT_TAIL_MS);
        portEXIT_CRITICAL(&ref_lock_);
        return active;
    }

protected:
    // Reads the raw 32-bit words instead of the base codec's int16 samples:
    // the low band (fan/TV rumble, mic low-frequency noise) is large enough to
    // clip the int16 conversion, so it is filtered out before saturating.
    int Read(int16_t* dest, int samples) override {
        const int channels = input_channels_;
        int frames = samples / channels;
        raw_.resize(frames);
        size_t bytes_read = 0;
        const esp_err_t read_result = i2s_channel_read(rx_handle_, raw_.data(),
                                                      frames * sizeof(int32_t), &bytes_read,
                                                      kReadTimeoutMs);
        if (read_result != ESP_OK) {
            // IDF may consume a partial read before timing out. Those samples
            // are discarded here, so the reference cursor must skip them too.
            portENTER_CRITICAL(&ref_lock_);
            nb_spk_ref_dropped(&ref_state_, bytes_read / sizeof(int32_t));
            portEXIT_CRITICAL(&ref_lock_);
#if CONFIG_NB3_AUDIO_DIAGNOSTICS
            read_errors_++;
#endif
            return 0;
        }
        frames = static_cast<int>(bytes_read / sizeof(int32_t));
        mic_.resize(frames);
        // Same scaling as the base codec (24-bit word >> 12); read every frame
        // so a runtime SetInputGain() takes effect.
        const float scale = powf(10.0f, input_gain_ / 20.0f);
        for (int i = 0; i < frames; i++) {
            float value = nb_hpf_step(&hpf_, static_cast<float>(raw_[i] >> 12)) * scale;
#if CONFIG_NB3_AUDIO_DIAGNOSTICS
            if (value > INT16_MAX || value < -INT16_MAX) clipped_samples_++;
#endif
            mic_[i] = value > INT16_MAX ? INT16_MAX
                      : value < -INT16_MAX ? -INT16_MAX
                                           : static_cast<int16_t>(value);
        }
        levels_.SetMic(AudioLevels::FromSamples(mic_.data(), frames), NowMs());

        if (channels == 1) {
            std::copy(mic_.begin(), mic_.end(), dest);
#if CONFIG_NB3_AUDIO_DIAGNOSTICS
            Diagnose(frames, false);
#endif
            return frames;
        }
        ref_.resize(frames);
        portENTER_CRITICAL(&ref_lock_);
        nb_spk_ref_read(&ref_state_, ref_.data(), frames);
        portEXIT_CRITICAL(&ref_lock_);
        for (int i = 0; i < frames; i++) {
            dest[2 * i] = mic_[i];
            dest[2 * i + 1] = ref_[i];
        }
#if CONFIG_NB3_AUDIO_DIAGNOSTICS
        Diagnose(frames, true);
#endif
        return frames * 2;
    }

    int Write(const int16_t* data, int samples) override {
        levels_.SetSpeaker(AudioLevels::FromSamples(data, samples), NowMs());
        const int written = NoAudioCodecDuplex::Write(data, samples);
        if (written > 0) {
            portENTER_CRITICAL(&ref_lock_);
            nb_output_activity_written(&output_activity_, NowMs());
            portEXIT_CRITICAL(&ref_lock_);
        }
        return written;
    }

    void EnableInput(bool enable) override {
        NoAudioCodecDuplex::EnableInput(enable);
        // Enabling the RX channel empties its queue.
        portENTER_CRITICAL(&ref_lock_);
        nb_spk_ref_input_restarted(&ref_state_);
        portEXIT_CRITICAL(&ref_lock_);
    }

    void EnableOutput(bool enable) override {
        NoAudioCodecDuplex::EnableOutput(enable);
        portENTER_CRITICAL(&ref_lock_);
        nb_spk_ref_set_output(&ref_state_, enable);
        portEXIT_CRITICAL(&ref_lock_);
    }

private:
    static uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

#if CONFIG_NB3_AUDIO_DIAGNOSTICS
    void Diagnose(int frames, bool has_ref) {
        for (int i = 0; i < frames; i++) {
            mic_peak_ = std::max(mic_peak_, std::abs(static_cast<int>(mic_[i])));
            if (has_ref) ref_peak_ = std::max(ref_peak_, std::abs(static_cast<int>(ref_[i])));
        }
        diagnostic_samples_ += frames;
        const uint32_t now = NowMs();
        if (now - diagnostic_ms_ < 5000) return;
        nb_spk_ref_t state;
        portENTER_CRITICAL(&ref_lock_);
        state = ref_state_;
        portEXIT_CRITICAL(&ref_lock_);
        ESP_LOGI(TAG, "AudioDiag n=%lu mic_peak=%d ref_peak=%d clips=%lu errors=%lu "
                      "ref=%d synced=%d offset=%ld range=%ld..%ld changes=%lu missing=%lu dropped=%lu",
                 (unsigned long)diagnostic_samples_, mic_peak_, ref_peak_,
                 (unsigned long)clipped_samples_, (unsigned long)read_errors_, has_ref, state.synced,
                 (long)state.offset, (long)state.offset_min, (long)state.offset_max,
                 (unsigned long)state.offset_changes, (unsigned long)state.unavailable_samples,
                 (unsigned long)state.dropped_samples);
        diagnostic_ms_ = now;
        diagnostic_samples_ = clipped_samples_ = 0;
        mic_peak_ = ref_peak_ = 0;
    }
    uint32_t diagnostic_ms_ = 0;
    uint32_t diagnostic_samples_ = 0;
    uint32_t clipped_samples_ = 0;
    uint32_t read_errors_ = 0;
    int mic_peak_ = 0;
    int ref_peak_ = 0;
#endif

    void StartReference() {
        // 4096 samples is ~170 ms at 24 kHz: more than the TX DMA queue plus
        // any reader lag the reference has to reach back over.
        constexpr uint32_t kHistory = 4096;
        auto* history = static_cast<int16_t*>(
            heap_caps_malloc(kHistory * sizeof(int16_t), MALLOC_CAP_SPIRAM));
        if (history == nullptr) {
            ESP_LOGE(TAG, "No memory for the AEC reference: AEC stays without it");
            return;
        }
        nb_spk_ref_init(&ref_state_, history, kHistory, AUDIO_AEC_REFERENCE_DELAY);
        i2s_event_callbacks_t tx_cbs = {};
        tx_cbs.on_sent = OnSent;
        i2s_event_callbacks_t rx_cbs = {};
        rx_cbs.on_recv = OnRecv;
        rx_cbs.on_recv_q_ovf = OnRecvOverflow;
        ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx_handle_, &tx_cbs, this));
        ESP_ERROR_CHECK(i2s_channel_register_event_callback(rx_handle_, &rx_cbs, this));
        input_reference_ = true;
        input_channels_ = 2;
        ESP_LOGI(TAG, "AEC reference from I2S DMA (delay %d samples; acoustic calibration pending)",
                 AUDIO_AEC_REFERENCE_DELAY);
    }

    // The TX buffer still holds what was just played (it is cleared after
    // this callback): the 32-bit words are the volume-scaled samples << 16.
    // I2S ISR callbacks, defined out of the class: IRAM functions must not be
    // inline.
    static bool OnSent(i2s_chan_handle_t, i2s_event_data_t* event, void* ctx);
    static bool OnRecv(i2s_chan_handle_t, i2s_event_data_t* event, void* ctx);
    static bool OnRecvOverflow(i2s_chan_handle_t, i2s_event_data_t* event, void* ctx);

    static constexpr uint32_t kReadTimeoutMs = 200;  // as the base codec
    AudioLevels& levels_;
    nb_hpf_t hpf_;
    std::vector<int32_t> raw_;
    std::vector<int16_t> mic_;
    std::vector<int16_t> ref_;
    nb_spk_ref_t ref_state_ = {};
    nb_output_activity_t output_activity_ = {};
    mutable portMUX_TYPE ref_lock_ = portMUX_INITIALIZER_UNLOCKED;
};

bool IRAM_ATTR NoiseBotV3AudioCodec::OnSent(i2s_chan_handle_t, i2s_event_data_t* event,
                                            void* ctx) {
    auto* self = static_cast<NoiseBotV3AudioCodec*>(ctx);
    const int32_t* words = static_cast<const int32_t*>(event->dma_buf);
    size_t n = event->size / sizeof(int32_t);
    int16_t chunk[64];
    bool signal = false;
    portENTER_CRITICAL_ISR(&self->ref_lock_);
    while (n > 0) {
        const size_t m = n < 64 ? n : 64;
        for (size_t i = 0; i < m; i++) {
            chunk[i] = static_cast<int16_t>(words[i] >> 16);
            signal = signal || chunk[i] != 0;
        }
        nb_spk_ref_played(&self->ref_state_, chunk, m);
        words += m;
        n -= m;
    }
    if (signal) {
        nb_output_activity_played(&self->output_activity_, NowMs());
    }
    portEXIT_CRITICAL_ISR(&self->ref_lock_);
    return false;
}

bool IRAM_ATTR NoiseBotV3AudioCodec::OnRecv(i2s_chan_handle_t, i2s_event_data_t* event,
                                            void* ctx) {
    auto* self = static_cast<NoiseBotV3AudioCodec*>(ctx);
    portENTER_CRITICAL_ISR(&self->ref_lock_);
    nb_spk_ref_captured(&self->ref_state_, event->size / sizeof(int32_t));
    portEXIT_CRITICAL_ISR(&self->ref_lock_);
    return false;
}

bool IRAM_ATTR NoiseBotV3AudioCodec::OnRecvOverflow(i2s_chan_handle_t, i2s_event_data_t* event,
                                                    void* ctx) {
    auto* self = static_cast<NoiseBotV3AudioCodec*>(ctx);
    portENTER_CRITICAL_ISR(&self->ref_lock_);
    nb_spk_ref_dropped(&self->ref_state_, event->size / sizeof(int32_t));
    portEXIT_CRITICAL_ISR(&self->ref_lock_);
    return false;
}

// Firmware defaults, overridden by the personality file on the microSD when
// there is one (VIDA.md §4).
static nb_personality_t LoadPersonality(StoreService& store) {
    nb_personality_t personality = nb_personality_default();
    std::string text;
    if (!store.ReadText("personalidade.ini", &text)) {
        ESP_LOGI(TAG, "no personality file: firmware defaults");
        return personality;
    }
    std::vector<char> decoded(text.size() + 1);
    nb_personality_text(reinterpret_cast<const uint8_t*>(text.data()), text.size(),
                        decoded.data(), decoded.size());
    const nb_personality_parse_result_t r = nb_personality_parse(decoded.data(), &personality);
    ESP_LOGI(TAG, "personality file (%u bytes): %d applied, %d clamped, %d rejected, %d unknown",
             static_cast<unsigned>(text.size()), r.applied, r.clamped, r.rejected, r.unknown);
    return personality;
}

class NoiseBotV3Board : public WifiBoard {
private:
    Button boot_button_;
    ExpressionArbiter arbiter_{NB_EXPR_NEUTRAL};
    StoreService store_;
    nb_personality_t personality_ = LoadPersonality(store_);
    MoodModifier mood_{arbiter_, personality_};
    BehaviorModifier behavior_{arbiter_};
    ClockService clock_;
    MemoryService memory_{store_, clock_, mood_};
    AudioLevels audio_levels_;
    FaceDisplay* display_ = nullptr;
    static constexpr uint32_t kMissingReminderMs = 5 * 60 * 1000;
    int missing_notices_ = 0;           // store task only
    uint32_t last_missing_notice_ms_ = 0;

    static const char* ResetReasonName(esp_reset_reason_t reason) {
        switch (reason) {
            case ESP_RST_POWERON: return "power on";
            case ESP_RST_SW: return "restart";
            case ESP_RST_PANIC: return "panic";
            case ESP_RST_INT_WDT: return "interrupt watchdog";
            case ESP_RST_TASK_WDT: return "task watchdog";
            case ESP_RST_WDT: return "watchdog";
            case ESP_RST_BROWNOUT: return "brownout";
            case ESP_RST_DEEPSLEEP: return "deep sleep";
            case ESP_RST_EXT: return "external pin";
            default: return "other";
        }
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_PCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        display_ = new FaceDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                   DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                   DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, arbiter_,
                                   mood_, audio_levels_);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    NoiseBotV3Board() : boot_button_(BOOT_BUTTON_GPIO) {
        // A crash reboots in a second and looks like a normal boot on the
        // face; the log says which it was.
        ESP_LOGI(TAG, "last reset: %s", ResetReasonName(esp_reset_reason()));
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        static InitiativeModifier initiative(arbiter_, behavior_, personality_);
        // Before the card listener: the memory hands its habits over once loaded.
        static PersonalityDrift drift(personality_, mood_, initiative);
        memory_.SetHabitsListener(
            [](const nb_mem_patterns_t& habits, int64_t now_s, bool now_known) {
                drift.OnHabits(habits, now_s, now_known);
            });
        // Without a card: warn at boot, again at the first retry (the boot
        // notices tend to cover the first one), then every few minutes.
        static SoundBank sounds(store_);
        store_.SetListener([this](bool available) {
            memory_.OnCard(available);
            sounds.OnCard(available);
            if (available) {
                missing_notices_ = 0;
                return;
            }
            const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            if (missing_notices_ < 2 || now - last_missing_notice_ms_ >= kMissingReminderMs) {
                missing_notices_++;
                last_missing_notice_ms_ = now;
                display_->ShowNotification("Memória indisponível: sem microSD", 8000);
            }
        });
        static EnergyModifier energy(arbiter_, behavior_, clock_, initiative, display_);
        RegisterFaceTools(arbiter_);
        RegisterMemoryTools(memory_, mood_, behavior_, energy);
        static PowerCutTest power_cut(store_, display_);  // bench builds only
        power_cut.Start();
        static ArcModifier arcs(arbiter_, behavior_, mood_, energy, display_);
        static MotionService motion(SERVO_ID_PAN, SERVO_ID_TILT, &arbiter_, &sounds);
        static HeadBehavior head(motion, display_, behavior_, energy);
        RegisterHeadTools(motion, head);
        StartMotionBench(motion);  // bench builds only
        static TouchModifier touch(BODY_TOUCH_CHANNEL, arbiter_, mood_, behavior_, memory_, arcs,
                                   head);
        static CpuReport cpu_report(display_);
        static LinkMonitor link(display_, &sounds);
        static ReminderService reminders(clock_, arbiter_, display_, head, link, sounds);
        RegisterReminderTools(reminders);
        static MoodLights mood_lights(MOOD_LED_GPIO, MOOD_LED_COUNT, MOOD_LED_MAX_BRIGHTNESS,
                                      arbiter_);
    }

    Led* GetLed() override {
        static SingleLed status_led(BUILTIN_LED_GPIO);
        static StateLed led(status_led, arbiter_, mood_, behavior_, memory_);
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
        static NoiseBotV3AudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                                AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                                                AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                                                audio_levels_);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(NoiseBotV3Board);
