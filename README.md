# batmon5

Battery monitor for a camper, built on the lcdwiki **ES3C28P** board: an
ESP32-S3 with a 2.8" touchscreen.

> **Status:** early bring-up. The firmware shows the overview screen
> (battery, solar, water tanks; design in [docs/ui/overview.svg](docs/ui/overview.svg))
> with sample values. No sensors are connected yet.

## Hardware

| Part      | Details                                                      |
|-----------|--------------------------------------------------------------|
| MCU       | ESP32-S3 N16R8: 16 MB quad flash, 8 MB octal PSRAM           |
| Display   | 2.8" ILI9341V IPS, 240×320, SPI (used as 320×240 landscape)  |
| Touch     | FT6336G capacitive, I2C                                      |
| Other     | ES8311 audio codec, microSD, WS2812 LED, battery ADC         |

- Board page: <https://www.lcdwiki.com/2.8inch_ESP32-S3_Display>
- Schematic: <https://www.lcdwiki.com/res/ES3C28P/2.8inch_ESP32-S3_Display_Schematic.pdf>

All pin assignments are in [main/board.h](main/board.h). The free GPIOs on the
expansion connector are IO2, IO3, IO14 and IO21.

## Building

Requirements: [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **v6.1.1**.
Third-party components (LVGL 9, esp_lvgl_port, ILI9341 and FT5x06 drivers)
are downloaded automatically from the ESP Component Registry on the first
build.

```bash
source ~/.espressif/tools/activate_idf_v6.1.1.sh   # or your own IDF export script
idf.py build
idf.py -p /dev/ttyACM0 flash monitor               # Ctrl+] exits the monitor
```

The board connects through its built-in USB-Serial/JTAG port. On WSL, attach
it first from Windows with `usbipd attach --wsl --busid <id>`.

### Backing up the factory firmware

Flashing overwrites the demo firmware the board ships with. To save a copy
first:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 read_flash 0 0x1000000 firmware-backup/factory.bin
```

To restore it:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0 firmware-backup/factory.bin
```

`firmware-backup/` is git-ignored.

## Configuration

- [sdkconfig.defaults](sdkconfig.defaults) holds the project settings: flash
  and PSRAM, the partition table, the USB console and the LVGL fonts.
  `sdkconfig` is generated from it. After changing the defaults, regenerate it
  with `rm sdkconfig && idf.py reconfigure`.
- [partitions.csv](partitions.csv) defines two 4 MB OTA app slots and a
  ~7.9 MB LittleFS `storage` partition.
- Display tuning flags in [main/board.h](main/board.h):
  `BOARD_LCD_INVERT_COLOR`, `BOARD_LCD_BGR_ORDER`, `BOARD_LCD_LANDSCAPE`,
  `BOARD_LCD_MIRROR_X` and `BOARD_LCD_MIRROR_Y`. Touch uses the same rotation
  and mirror settings, so the display and touch stay aligned. On the overview
  screen, the SOC arc and battery power should be green, the solar icon yellow
  and the fresh-water bar blue.

## Project layout

```
main/
  batmon5.c        app_main()
  ui_overview.c/.h overview screen (battery, solar, water)
  display.c/.h     backlight, ILI9341 (SPI2), FT6336 touch (I2C0), LVGL port
  board.h          board pins and panel settings
  idf_component.yml
docs/ui/           screen designs (SVG)
partitions.csv
sdkconfig.defaults
```

LVGL runs in its own task, started by esp_lvgl_port. Any LVGL call made from
another task has to be wrapped in `lvgl_port_lock(0)` / `lvgl_port_unlock()`.
