#include "display.h"
#include "board.h"

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

static const char *TAG = "display";

#if BOARD_LCD_LANDSCAPE
#define LCD_H_RES BOARD_LCD_NATIVE_V_RES
#define LCD_V_RES BOARD_LCD_NATIVE_H_RES
#else
#define LCD_H_RES BOARD_LCD_NATIVE_H_RES
#define LCD_V_RES BOARD_LCD_NATIVE_V_RES
#endif

/* LVGL draw buffer: 1/10 of the screen, double-buffered in DMA-capable RAM. */
#define LCD_DRAW_BUF_LINES 24

static esp_err_t backlight_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "backlight gpio");
    return gpio_set_level(BOARD_LCD_PIN_BL, 0);
}

static esp_err_t lcd_init(esp_lcd_panel_io_handle_t *out_io, esp_lcd_panel_handle_t *out_panel)
{
    spi_bus_config_t bus_cfg = ILI9341_PANEL_BUS_SPI_CONFIG(
        BOARD_LCD_PIN_SCLK, BOARD_LCD_PIN_MOSI,
        LCD_H_RES * LCD_DRAW_BUF_LINES * sizeof(uint16_t));
    bus_cfg.miso_io_num = BOARD_LCD_PIN_MISO;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg =
        ILI9341_PANEL_IO_SPI_CONFIG(BOARD_LCD_PIN_CS, BOARD_LCD_PIN_DC, NULL, NULL);
    io_cfg.pclk_hz = BOARD_LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                                                 &io_cfg, out_io),
                        TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order = BOARD_LCD_BGR_ORDER ? LCD_RGB_ELEMENT_ORDER_BGR
                                             : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*out_io, &panel_cfg, out_panel),
                        TAG, "ili9341 panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(*out_panel, BOARD_LCD_INVERT_COLOR),
                        TAG, "invert");
    return esp_lcd_panel_disp_on_off(*out_panel, true);
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *out_touch)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus");

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io), TAG, "touch io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_NATIVE_H_RES,
        .y_max = BOARD_LCD_NATIVE_V_RES,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = {
            .swap_xy = BOARD_LCD_LANDSCAPE,
            .mirror_x = BOARD_LCD_MIRROR_X,
            .mirror_y = BOARD_LCD_MIRROR_Y,
        },
    };
    return esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, out_touch);
}

esp_err_t display_init(lv_display_t **out_disp)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_handle_t lcd_panel;
    ESP_RETURN_ON_ERROR(lcd_init(&lcd_io, &lcd_panel), TAG, "lcd");

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = BOARD_LCD_LANDSCAPE,
            .mirror_x = BOARD_LCD_MIRROR_X,
            .mirror_y = BOARD_LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl add disp");

    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t err = touch_init(&touch);
    if (err == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = touch,
        };
        if (!lvgl_port_add_touch(&touch_cfg)) {
            ESP_LOGW(TAG, "lvgl add touch failed");
        }
    } else {
        ESP_LOGW(TAG, "touch init failed (%s), continuing without touch", esp_err_to_name(err));
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_PIN_BL, 1), TAG, "backlight on");
    ESP_LOGI(TAG, "display %dx%d ready", LCD_H_RES, LCD_V_RES);

    if (out_disp) {
        *out_disp = disp;
    }
    return ESP_OK;
}
