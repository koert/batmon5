#include "display.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "batmon5";

static lv_obj_t *s_touch_label;

static void screen_touch_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_label_set_text_fmt(s_touch_label, "touch: %d, %d", (int)p.x, (int)p.y);
}

static void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_clickable(scr, true);
    lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "batmon5");
    lv_obj_set_style_text_color(title, lv_color_hex(0x9AA5B1), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 10);

    /* Placeholder for the house battery voltage. */
    lv_obj_t *volts = lv_label_create(scr);
    lv_label_set_text(volts, "--.-- V");
    lv_obj_set_style_text_color(volts, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(volts, &lv_font_montserrat_48, 0);
    lv_obj_align(volts, LV_ALIGN_CENTER, 0, -10);

    /* Colour check bar: should read red, green, blue from left to right. */
    static const uint32_t colors[] = { 0xFF0000, 0x00FF00, 0x0000FF };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, 40, 16);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(colors[i]), 0);
        lv_obj_align(box, LV_ALIGN_TOP_RIGHT, -12 - (2 - i) * 44, 16);
    }

    s_touch_label = lv_label_create(scr);
    lv_label_set_text(s_touch_label, "touch the screen");
    lv_obj_set_style_text_color(s_touch_label, lv_color_hex(0x9AA5B1), 0);
    lv_obj_align(s_touch_label, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init(NULL));

    lvgl_port_lock(0);
    ui_create();
    lvgl_port_unlock();

    ESP_LOGI(TAG, "started");
}
