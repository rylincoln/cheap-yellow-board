# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Layout

PlatformIO projects in ESP-IDF C for the ESP32 Cheap Yellow Display. `launcher/` holds the factory app, a custom second-stage bootloader (`bootloader_components/`) and `partitions.csv`. Each folder under `apps/` is one boot image. Every project pulls in the one shared `components/cyd_display` through `EXTRA_COMPONENT_DIRS` in its top-level `CMakeLists.txt`.

An app's CMake `project()` name must equal its folder name. CI tags each app's release with the folder name, and the launcher treats a slot whose project name differs from the tag it was downloaded as as flashed over USB, so it offers the same build again forever.

## Build, flash, monitor (macOS)

The projects were written on Windows: `build_dir` in each `platformio.ini` is under `${sysenv.USERPROFILE}`, which macOS leaves unset. Use the PlatformIO IDE's own `pio`; the one first on PATH (a uv tool install) lacks `intelhex` and fails while packaging the launcher's bootloader.

```sh
cd launcher   # or apps/<name>
USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run
```

Flash the launcher with esptool. `pio run --target upload` writes a stock bootloader over the custom one.

```sh
PY=~/.platformio/penv/bin/python
ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
L=~/.platformio/cyd-launcher-build/esp32dev
P=/dev/cu.usbserial-110
$PY $ESPTOOL --chip esp32 -p $P erase_flash
$PY $ESPTOOL --chip esp32 -p $P -b 460800 write_flash -z 0x1000 $L/bootloader.bin \
    0x8000 $L/partitions.bin 0xd000 $L/ota_data_initial.bin 0x10000 $L/firmware.bin
```

An app's `firmware.bin` goes to `0x150000` (slot A) or `0x2A0000` (slot B). Erasing also clears the Wi-Fi settings, so the launcher opens its network chooser on the next start.

Run serial commands (esptool, monitor) with the Bash sandbox disabled. The board's CH340 port on this Mac sometimes wedges, failing every open with termios "Invalid argument" until the board is unplugged and replugged.

There are no tests; a clean build is the check. After changing a project's `sdkconfig.defaults`, delete its generated `sdkconfig.esp32dev` so the new defaults apply.

## Architecture

- **Boot.** On power-on the custom bootloader always starts the launcher. Any other reset goes to the image chosen in `otadata`, so a crashing app restarts itself rather than the launcher.
- **Launcher.** A menu of the two slots counts down to the last-used image while a network task joins Wi-Fi and lists the releases of `IMAGE_REPO` that carry `firmware.bin`. Tapping one downloads it into a slot and boots it.
- **Settings.** NVS namespace `cyd_config` is shared by every image: `wifi_ssid`, `wifi_pass`, `wildlife_url`, `ha_url`, `ha_token`, `ha_upstairs`, `ha_downstairs`, `ha_outside`, `ha_battery`, `brightness`, `touch_cal`. No binary carries credentials. The launcher's network chooser and its phone setup write them. Phone setup (`launcher/main/phone_setup.c`) runs an open access point, `CYD-Setup-XXXX`, a DNS server that answers every name with the board, and one web page.
- **Display.** `cyd_display` reads the panel's RDID4 register (0xD3) at start-up and picks the 3.5" ST7796 or 2.8" ILI9341 profile, falling back to the 2.8" one. Screen size, font scale and text columns are runtime values, so file-scope arrays use `CYD_LCD_MAX_WIDTH`, `CYD_TEXT_MAX_COLUMNS` and the like. Text is a 10-row grid of 6x8 cells scaled 3x or 4x. Pixel data is sent column-major: the column address (0x2A) spans y.
- **Memory.** The boards have no PSRAM; about 320 KB of DRAM is free for data. Stream images through the drawing calls; a full 480x320 frame buffer doesn't fit.

## Hardware

The user has two 3.5" ESP32-3248S035R boards, verified on hardware with the launcher, phone setup and Wi-Fi. They also have a 2.8" ESP32-2432S028 with two USB ports, not yet tested; that revision may carry an ST7789, which doesn't answer RDID4 the same way and would need its own profile.
