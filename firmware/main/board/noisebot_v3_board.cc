#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
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
#include "core/nb_personality.h"

#include <cmath>
#include <vector>
#include <esp_log.h>
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
    }

protected:
    // Reads the raw 32-bit words instead of the base codec's int16 samples:
    // the low band (fan/TV rumble, mic low-frequency noise) is large enough to
    // clip the int16 conversion, so it is filtered out before saturating.
    int Read(int16_t* dest, int samples) override {
        raw_.resize(samples);
        size_t bytes_read = 0;
        if (i2s_channel_read(rx_handle_, raw_.data(), samples * sizeof(int32_t), &bytes_read,
                             kReadTimeoutMs) != ESP_OK) {
            return 0;
        }
        samples = static_cast<int>(bytes_read / sizeof(int32_t));
        // Same scaling as the base codec (24-bit word >> 12); read every frame
        // so a runtime SetInputGain() takes effect.
        const float scale = powf(10.0f, input_gain_ / 20.0f);
        for (int i = 0; i < samples; i++) {
            float value = nb_hpf_step(&hpf_, static_cast<float>(raw_[i] >> 12)) * scale;
            dest[i] = value > INT16_MAX ? INT16_MAX
                      : value < -INT16_MAX ? -INT16_MAX
                                           : static_cast<int16_t>(value);
        }
        levels_.SetMic(AudioLevels::FromSamples(dest, samples), NowMs());
        return samples;
    }

    int Write(const int16_t* data, int samples) override {
        levels_.SetSpeaker(AudioLevels::FromSamples(data, samples), NowMs());
        return NoAudioCodecDuplex::Write(data, samples);
    }

private:
    static uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

    static constexpr uint32_t kReadTimeoutMs = 200;  // as the base codec
    AudioLevels& levels_;
    nb_hpf_t hpf_;
    std::vector<int32_t> raw_;
};

// Firmware defaults, overridden by the personality file on the microSD when
// there is one (VIDA.md §4).
static nb_personality_t LoadPersonality(StoreService& store) {
    nb_personality_t personality = nb_personality_default();
    std::string text;
    if (!store.ReadText("personalidade.ini", &text)) {
        ESP_LOGI(TAG, "no personality file: firmware defaults");
        return personality;
    }
    const nb_personality_parse_result_t r = nb_personality_parse(text.c_str(), &personality);
    ESP_LOGI(TAG, "personality file: %d applied, %d clamped, %d rejected, %d unknown",
             r.applied, r.clamped, r.rejected, r.unknown);
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
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        // Without a card: warn at boot, again at the first retry (the boot
        // notices tend to cover the first one), then every few minutes.
        store_.SetListener([this](bool available) {
            memory_.OnCard(available);
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
        static InitiativeModifier initiative(arbiter_, behavior_, personality_);
        static EnergyModifier energy(arbiter_, behavior_, clock_, initiative, display_);
        RegisterFaceTools(arbiter_);
        RegisterMemoryTools(memory_, mood_, behavior_, energy);
        static PowerCutTest power_cut(store_, display_);  // bench builds only
        power_cut.Start();
        static TouchModifier touch(BODY_TOUCH_CHANNEL, arbiter_, mood_, behavior_, memory_);
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
