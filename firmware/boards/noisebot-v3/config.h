#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Waveshare ESP32-S3-DEV-KIT N32R16V. GPIO47/48 are on the 1.8 V VDD_SPI
// domain on this module and must never be used for 3.3 V logic.

#include <driver/gpio.h>

// Mic and amp share BCLK/WS in duplex mode, so both directions run at the
// output rate; the audio service resamples the mic down to 16 kHz.
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_39  // INMP441 SD
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_40
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_41
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_42  // MAX98357 DIN

#define BUILTIN_LED_GPIO  GPIO_NUM_38    // onboard WS2812
#define BOOT_BUTTON_GPIO  GPIO_NUM_0

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
