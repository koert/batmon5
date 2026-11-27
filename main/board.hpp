/*
 * Pin and panel definitions for the lcdwiki ES3C28P board
 * (ESP32-S3 N16R8, 2.8" ILI9341V 240x320 IPS, FT6336G capacitive touch).
 * Source: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
 */
#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "hal/spi_types.h"

namespace board {

/* ---- LCD: ILI9341V, 4-wire SPI ---- */
inline constexpr spi_host_device_t kLcdSpiHost = SPI2_HOST;
inline constexpr gpio_num_t kLcdPinSclk = GPIO_NUM_12;
inline constexpr gpio_num_t kLcdPinMosi = GPIO_NUM_11;
inline constexpr gpio_num_t kLcdPinMiso = GPIO_NUM_13;
inline constexpr gpio_num_t kLcdPinCs   = GPIO_NUM_10;
inline constexpr gpio_num_t kLcdPinDc   = GPIO_NUM_46;
inline constexpr gpio_num_t kLcdPinRst  = GPIO_NUM_NC;   // tied to the chip's EN/reset
inline constexpr gpio_num_t kLcdPinBl   = GPIO_NUM_45;   // high = backlight on
inline constexpr uint32_t kLcdPclkHz = 40 * 1000 * 1000;

/* Panel native orientation is 240x320 portrait. */
inline constexpr int kLcdNativeHRes = 240;
inline constexpr int kLcdNativeVRes = 320;

/* Panel colour quirks: adjust if colours look wrong on the hardware.
 * IPS panels usually need inversion; red/blue swapped -> flip BGR. */
inline constexpr bool kLcdInvertColor = true;
inline constexpr bool kLcdBgrOrder = true;

/* Landscape (320x240): swap XY. If the image is mirrored or upside down,
 * flip kLcdMirrorX / kLcdMirrorY; touch follows the same settings, plus the
 * touch-only flips below. */
inline constexpr bool kLcdLandscape = true;
inline constexpr bool kLcdMirrorX = false;
inline constexpr bool kLcdMirrorY = false;

/* ---- Touch: FT6336G on I2C (bus shared with ES8311 codec + IIC connector) ---- */
/* Extra touch flips on top of the display's, on the panel's native portrait axes
 * (applied before the landscape swap: native X = screen Y). Checked on the
 * hardware: the FT6336's native X axis runs opposite to the display's, so in
 * landscape touch was upside down. */
inline constexpr bool kTouchFlipNativeX = true;
inline constexpr bool kTouchFlipNativeY = false;

inline constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;
inline constexpr gpio_num_t kI2cPinSda = GPIO_NUM_16;
inline constexpr gpio_num_t kI2cPinScl = GPIO_NUM_15;
inline constexpr gpio_num_t kTouchPinRst = GPIO_NUM_18;
inline constexpr gpio_num_t kTouchPinInt = GPIO_NUM_17;

/* ---- Audio: ES8311 codec over I2S, FM8002E amplifier ---- */
inline constexpr gpio_num_t kAudioPinMclk = GPIO_NUM_4;
inline constexpr gpio_num_t kAudioPinBclk = GPIO_NUM_5;
inline constexpr gpio_num_t kAudioPinWs   = GPIO_NUM_7;
inline constexpr gpio_num_t kAudioPinDout = GPIO_NUM_8;
inline constexpr gpio_num_t kAudioPinDin  = GPIO_NUM_6;
inline constexpr gpio_num_t kAudioPinPaEn = GPIO_NUM_1;  // low = amplifier on

/* ---- microSD, SDIO 4-bit (data-line order to be confirmed from schematic) ---- */
inline constexpr gpio_num_t kSdPinClk = GPIO_NUM_38;
inline constexpr gpio_num_t kSdPinCmd = GPIO_NUM_40;

/* ---- Misc ---- */
inline constexpr gpio_num_t kRgbLedPin  = GPIO_NUM_42;   // WS2812B
inline constexpr gpio_num_t kBatAdcPin  = GPIO_NUM_9;    // board Li-ion battery divider
inline constexpr gpio_num_t kBootBtnPin = GPIO_NUM_0;

/* Free GPIOs on the 4-pin expansion connector: 2, 3, 14, 21 */

}  // namespace board
