#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Waveshare ESP32-S3-DEV-KIT N32R16V. GPIO47/48 are on the 1.8 V VDD_SPI
// domain on this module and must never be used for 3.3 V logic.

#include <driver/gpio.h>

// Mic and amp share BCLK/WS in duplex mode, so both directions run at the
// output rate; the audio service resamples the mic down to 16 kHz.
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// Mic gain in dB, applied digitally on top of the codec's fixed x16 (>> 12 of
// the 24-bit INMP441 word); the INMP441 has no PGA to set it in hardware.
// Kept at 0: the current INMP441 batch has a ~-40 dBFS noise floor, so any
// gain amplifies the noise as much as the voice.
#define AUDIO_INPUT_GAIN_DB 0.0f

// High-pass cutoff for the mic. Recordings show most of the noise energy
// below 150 Hz while speech sits at 300-3400 Hz.
#define AUDIO_INPUT_HPF_HZ 150.0f

// I2S slot the INMP441 drives, set by its L/R pin (GND = left, VDD = right).
#define AUDIO_I2S_MIC_SLOT I2S_STD_SLOT_LEFT

#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_39  // INMP441 SD
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_40
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_41
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_42  // MAX98357 DIN

#define BUILTIN_LED_GPIO  GPIO_NUM_38    // onboard WS2812
#define BOOT_BUTTON_GPIO  GPIO_NUM_0

// Copper-tape body pad on GPIO2 = touch channel 2.
#define BODY_TOUCH_CHANNEL 2

// External WS2812 body LEDs. The brightness cap keeps them from glaring next
// to the face; the data line is driven at 3.3 V without a level shifter.
#define MOOD_LED_GPIO           GPIO_NUM_21
#define MOOD_LED_COUNT          2
#define MOOD_LED_MAX_BRIGHTNESS 6

// ST7789 2" on SPI2; RST and backlight are hardwired, reset is SWRESET.
#define DISPLAY_SPI_HOST    SPI2_HOST
#define DISPLAY_CS_PIN      GPIO_NUM_10
#define DISPLAY_MOSI_PIN    GPIO_NUM_11
#define DISPLAY_CLK_PIN     GPIO_NUM_12
#define DISPLAY_DC_PIN      GPIO_NUM_14
#define DISPLAY_RST_PIN     GPIO_NUM_NC
#define DISPLAY_PCLK_HZ     (40 * 1000 * 1000)
#define DISPLAY_SPI_MODE    0

// Landscape: the panel is 240x320 native, rotated by swap + mirror.
#define DISPLAY_WIDTH        320
#define DISPLAY_HEIGHT       240
#define DISPLAY_SWAP_XY      true
#define DISPLAY_MIRROR_X     true
#define DISPLAY_MIRROR_Y     false
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER    LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X     0
#define DISPLAY_OFFSET_Y     0

#endif // _BOARD_CONFIG_H_
