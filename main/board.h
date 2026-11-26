/*
 * Pin and panel definitions for the lcdwiki ES3C28P board
 * (ESP32-S3 N16R8, 2.8" ILI9341V 240x320 IPS, FT6336G capacitive touch).
 * Source: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
 */
#pragma once

#include "driver/gpio.h"

/* ---- LCD: ILI9341V, 4-wire SPI ---- */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_PIN_SCLK      GPIO_NUM_12
#define BOARD_LCD_PIN_MOSI      GPIO_NUM_11
#define BOARD_LCD_PIN_MISO      GPIO_NUM_13
#define BOARD_LCD_PIN_CS        GPIO_NUM_10
#define BOARD_LCD_PIN_DC        GPIO_NUM_46
#define BOARD_LCD_PIN_RST       GPIO_NUM_NC     /* tied to the chip's EN/reset */
#define BOARD_LCD_PIN_BL        GPIO_NUM_45     /* high = backlight on */
#define BOARD_LCD_PCLK_HZ       (40 * 1000 * 1000)

/* Panel native orientation is 240x320 portrait. */
#define BOARD_LCD_NATIVE_H_RES  240
#define BOARD_LCD_NATIVE_V_RES  320

/* Panel colour quirks: adjust if colours look wrong on the hardware.
 * IPS panels usually need inversion; red/blue swapped -> flip BGR. */
#define BOARD_LCD_INVERT_COLOR  1
#define BOARD_LCD_BGR_ORDER     1

/* Landscape (320x240): swap XY. If the image is mirrored or upside down,
 * flip the MIRROR_X / MIRROR_Y values; touch follows the same settings. */
#define BOARD_LCD_LANDSCAPE     1
#define BOARD_LCD_MIRROR_X      0
#define BOARD_LCD_MIRROR_Y      0

/* ---- Touch: FT6336G on I2C (bus shared with ES8311 codec + IIC connector) ---- */
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_PIN_SDA       GPIO_NUM_16
#define BOARD_I2C_PIN_SCL       GPIO_NUM_15
#define BOARD_TOUCH_PIN_RST     GPIO_NUM_18
#define BOARD_TOUCH_PIN_INT     GPIO_NUM_17

/* ---- Audio: ES8311 codec over I2S, FM8002E amplifier ---- */
#define BOARD_AUDIO_PIN_MCLK    GPIO_NUM_4
#define BOARD_AUDIO_PIN_BCLK    GPIO_NUM_5
#define BOARD_AUDIO_PIN_WS      GPIO_NUM_7
#define BOARD_AUDIO_PIN_DOUT    GPIO_NUM_8
#define BOARD_AUDIO_PIN_DIN     GPIO_NUM_6
#define BOARD_AUDIO_PIN_PA_EN   GPIO_NUM_1      /* low = amplifier on */

/* ---- microSD, SDIO 4-bit (data-line order to be confirmed from schematic) ---- */
#define BOARD_SD_PIN_CLK        GPIO_NUM_38
#define BOARD_SD_PIN_CMD        GPIO_NUM_40

/* ---- Misc ---- */
#define BOARD_RGB_LED_PIN       GPIO_NUM_42     /* WS2812B */
#define BOARD_BAT_ADC_PIN       GPIO_NUM_9      /* battery voltage divider */
#define BOARD_BOOT_BTN_PIN      GPIO_NUM_0

/* Free GPIOs on the 4-pin expansion connector: 2, 3, 14, 21 */
