#include "display.hpp"
#include "board.hpp"

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace display {
namespace {

constexpr const char *TAG = "display";

constexpr int kHRes = board::kLcdLandscape ? board::kLcdNativeVRes : board::kLcdNativeHRes;
constexpr int kVRes = board::kLcdLandscape ? board::kLcdNativeHRes : board::kLcdNativeVRes;

/* LVGL draw buffer: 1/10 of the screen, double-buffered in DMA-capable RAM. */
constexpr int kDrawBufLines = 24;

esp_err_t backlight_init()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << board::kLcdPinBl;
    cfg.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "backlight gpio");
    return gpio_set_level(board::kLcdPinBl, 0);
}

esp_err_t lcd_init(esp_lcd_panel_io_handle_t *out_io, esp_lcd_panel_handle_t *out_panel)
{
    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = board::kLcdPinSclk;
    bus_cfg.mosi_io_num = board::kLcdPinMosi;
    bus_cfg.miso_io_num = board::kLcdPinMiso;
    bus_cfg.quadwp_io_num = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = kHRes * kDrawBufLines * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(board::kLcdSpiHost, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = board::kLcdPinCs;
    io_cfg.dc_gpio_num = board::kLcdPinDc;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = board::kLcdPclkHz;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(board::kLcdSpiHost),
                                 &io_cfg, out_io),
        TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.rgb_ele_order = board::kLcdBgrOrder ? LCD_RGB_ELEMENT_ORDER_BGR
                                                  : LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.reset_gpio_num = board::kLcdPinRst;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*out_io, &panel_cfg, out_panel),
                        TAG, "ili9341 panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*out_panel, board::kLcdInvertColor),
                        TAG, "invert");
    return esp_lcd_panel_disp_on_off(*out_panel, true);
}

esp_err_t touch_init(esp_lcd_touch_handle_t *out_touch)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = board::kI2cPort;
    bus_cfg.sda_io_num = board::kI2cPinSda;
    bus_cfg.scl_io_num = board::kI2cPinScl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus");

    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    tp_io_cfg.scl_speed_hz = 100000;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset = 0;
    tp_io_cfg.lcd_cmd_bits = 8;
    tp_io_cfg.flags.disable_control_phase = 1;
    esp_lcd_panel_io_handle_t tp_io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io), TAG, "touch io");

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = board::kLcdNativeHRes;
    tp_cfg.y_max = board::kLcdNativeVRes;
    tp_cfg.rst_gpio_num = board::kTouchPinRst;
    tp_cfg.int_gpio_num = board::kTouchPinInt;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy = board::kLcdLandscape;
    tp_cfg.flags.mirror_x = board::kLcdMirrorX;
    tp_cfg.flags.mirror_y = board::kLcdMirrorY;
    return esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, out_touch);
}

}  // namespace

esp_err_t init(lv_display_t **out_disp)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_handle_t lcd_panel;
    ESP_RETURN_ON_ERROR(lcd_init(&lcd_io, &lcd_panel), TAG, "lcd");

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = lcd_io;
    disp_cfg.panel_handle = lcd_panel;
    disp_cfg.buffer_size = kHRes * kDrawBufLines;
    disp_cfg.double_buffer = true;
    disp_cfg.hres = kHRes;
    disp_cfg.vres = kVRes;
    disp_cfg.monochrome = false;
    disp_cfg.rotation.swap_xy = board::kLcdLandscape;
    disp_cfg.rotation.mirror_x = board::kLcdMirrorX;
    disp_cfg.rotation.mirror_y = board::kLcdMirrorY;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.swap_bytes = true;
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl add disp");

    esp_lcd_touch_handle_t touch = nullptr;
    esp_err_t err = touch_init(&touch);
    if (err == ESP_OK) {
        lvgl_port_touch_cfg_t touch_cfg = {};
        touch_cfg.disp = disp;
        touch_cfg.handle = touch;
        if (!lvgl_port_add_touch(&touch_cfg)) {
            ESP_LOGW(TAG, "lvgl add touch failed");
        }
    } else {
        ESP_LOGW(TAG, "touch init failed (%s), continuing without touch", esp_err_to_name(err));
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(board::kLcdPinBl, 1), TAG, "backlight on");
    ESP_LOGI(TAG, "display %dx%d ready", kHRes, kVRes);

    if (out_disp) {
        *out_disp = disp;
    }
    return ESP_OK;
}

}  // namespace display
