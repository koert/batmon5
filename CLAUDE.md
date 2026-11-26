# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

batmon5 is a camper battery monitor. It is ESP-IDF firmware for the lcdwiki **ES3C28P** board: an ESP32-S3 N16R8 (16 MB quad flash, 8 MB octal PSRAM) with a 2.8" ILI9341V 240×320 SPI IPS display, FT6336G capacitive touch, ES8311 audio codec, microSD, a WS2812 LED and a battery ADC. Board reference: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display (schematic: https://www.lcdwiki.com/res/ES3C28P/2.8inch_ESP32-S3_Display_Schematic.pdf).

## Environment

- The source lives in WSL Ubuntu (`~/project/camper/batmon5`), and the IDE runs on Windows and reaches it over `\\wsl.localhost\Ubuntu\...`. Run `idf.py` **inside WSL**, not from Windows/Git Bash. From the Windows side, use `wsl.exe -d Ubuntu -- bash <script>`; put multi-line commands in a script file, because Git Bash mangles `$vars` and paths passed inline.
- ESP-IDF **v6.1.1** was installed with EIM (`~/.local/bin/eim`) into `~/.espressif`. Activate it in every shell:
  `source ~/.espressif/tools/activate_idf_v6.1.1.sh`
- The board is connected over its built-in USB-Serial/JTAG and appears in WSL as `/dev/ttyACM0` after `usbipd attach --wsl --busid <id>` on Windows. If the port is missing, it has to be re-attached from Windows.
- `sudo` in WSL needs a password, so you can't run it non-interactively.

## Commands

```bash
idf.py build                              # build (output: build/batmon5.bin)
idf.py -p /dev/ttyACM0 flash monitor      # flash + serial monitor (Ctrl+] exits)
idf.py menuconfig                         # interactive config (writes sdkconfig)
idf.py add-dependency "<ns>/<component>"  # add a component-registry dependency to main/idf_component.yml
rm sdkconfig && idf.py reconfigure        # regenerate sdkconfig after editing sdkconfig.defaults
```

There are no tests or linter yet. A build without warnings is the bar.

## Configuration

- `sdkconfig.defaults` is the source of truth for project settings (flash size and mode, octal PSRAM, custom partitions, USB-JTAG console, LVGL fonts). `sdkconfig` is generated and git-ignored. Put lasting changes in `sdkconfig.defaults`, not only in menuconfig.
- `partitions.csv`: two 4 MB OTA slots (`ota_0` and `ota_1`, for future OTA updates) plus a ~7.9 MB `storage` partition of type littlefs.
- Third-party components come from the ESP Component Registry through `main/idf_component.yml`. They are pinned in `dependencies.lock` and downloaded into `managed_components/` (git-ignored; never edit them there). Current versions: LVGL 9.6.0, esp_lvgl_port 2.9.1, esp_lcd_ili9341 2.1.0, esp_lcd_touch_ft5x06 1.1.1 (used for the FT6336). This is the LVGL **v9** API, and older APIs are deprecated in 9.6 (for example, use `lv_obj_set_clickable()` instead of `lv_obj_add_flag(..., LV_OBJ_FLAG_CLICKABLE)`).

## Architecture

- `main/board.h` holds every GPIO and panel quirk for the board. Add new hardware pins here, never inline. It also has the display tuning switches (`BOARD_LCD_INVERT_COLOR`, `BOARD_LCD_BGR_ORDER`, `BOARD_LCD_LANDSCAPE`, `BOARD_LCD_MIRROR_X/Y`). These haven't been checked on real hardware yet. The touch controller uses the same rotation and mirror flags, so the display and touch always change together.
- `main/display.c` (`display_init()`) starts the backlight (GPIO, held off until init finishes), the ILI9341 on SPI2, the FT6336 on the new `i2c_master` driver (I2C0), and the `esp_lvgl_port` task, and registers touch as an LVGL input device. If touch fails to start, it only logs a warning.
- `main/batmon5.c`: `app_main()` calls `display_init()`, builds the overview screen and fills it with sample values (no sensors yet).
- `main/ui_overview.c`: the overview screen, built from `docs/ui/overview.svg` using absolute SVG coordinates (labels are placed by baseline). Update values with `ui_overview_set_battery()`, `_set_solar()` and `_set_water()`. These take the LVGL lock themselves, and NAN or a negative percentage shows "--".
- LVGL runs in the esp_lvgl_port task. **Any LVGL call made from another task must be wrapped in `lvgl_port_lock(0)` / `lvgl_port_unlock()`.**

## Hardware constraints

- The LCD reset pin is tied to chip EN (`GPIO_NUM_NC`), so there's no separate panel reset.
- I2C0 (SDA 16, SCL 15) is shared by touch, the ES8311 codec and the external IIC connector. Reuse the existing bus handle instead of creating a second bus on the same pins.
- GPIO 35–37 are used by the octal PSRAM. The free pins for sensors are IO2, IO3, IO14 and IO21 (expansion connector).
- The audio amplifier enable (IO1) is active-low.
- Flashing overwrites the factory demo firmware on the board.
