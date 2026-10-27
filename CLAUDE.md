# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Layout

PlatformIO projects in ESP-IDF C for the ESP32 Cheap Yellow Display. `launcher/` holds the factory app, a custom second-stage bootloader (`bootloader_components/`) and `partitions.csv`. Each folder under `apps/` is one boot image. The dashboards share `components/ha_glance` (Home Assistant readings and tiles) and `components/wildlife` (birds on tap). Every project pulls in the one shared `components/cyd_display` through `EXTRA_COMPONENT_DIRS` in its top-level `CMakeLists.txt`.

An app's CMake `project()` name must equal its folder name. CI tags each app's release with the folder name, and the launcher treats a slot whose project name differs from the tag it was downloaded as as flashed over USB, so it offers the same build again forever.

## Build, flash, monitor (macOS)

The projects were written on Windows: `build_dir` in each `platformio.ini` is under `${sysenv.USERPROFILE}`, which macOS leaves unset. Use the PlatformIO IDE's own `pio`; the one first on PATH (a uv tool install) lacks `intelhex` and fails while packaging the launcher's bootloader.

```sh
cd launcher   # or apps/<name>
USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run
```

`pio run -t upload` in `launcher/` or an app folder pushes the build to the running app over Wi-Fi with `tools/cyd_push.py`; `docs/INSTALL.md` section 7 has its setup. Never run `--show-key`.

USB is the recovery path and writes the bootloader, which a push can't. Flash the launcher with esptool.

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

Run `sh test/run.sh` for the host-tested pure modules (plain C, built with the system `cc`) and `tools/test_cyd_push.py`, then a clean build. `tools/make_bigfont.py` regenerates both dashboards' `bigfont_data.c`; its docstring has the pinned command. After changing a project's `sdkconfig.defaults`, delete its generated `sdkconfig.esp32dev` so the new defaults apply. Delete it too after adding source files or an `idf_component.yml` under `components/`: PlatformIO re-runs CMake only when a project's own `CMakeLists.txt` or `sdkconfig` changes. A changed bootloader setting also needs `~/.platformio/cyd-launcher-build/esp32dev/bootloader` deleted, since that sub-build ignores a regenerated `sdkconfig.esp32dev`.

## Architecture

- **Boot.** On power-on the custom bootloader starts the launcher if it verifies, else the image `otadata` selects. Any other reset goes to the image chosen in `otadata`, so a crashing app restarts itself rather than the launcher. A new build is on trial: it is kept after 60 s of uptime and its first screen, else the next restart rolls back, and the launcher's countdown no longer defaults to that slot. An app must include `cyd_update` or call `esp_ota_mark_app_valid_cancel_rollback()`.
- **Launcher.** A menu of the two slots counts down to the last-used image while a network task joins Wi-Fi and lists the releases of `IMAGE_REPO` that carry `firmware.bin`, except `launcher`. Tapping one downloads it into a slot and boots it.
- **Updates.** `components/cyd_update`, in each app, takes pushes from the Mac (`push_server.c`) and checks the `launcher` release and the app's own release when the network comes up, then hourly (`github_check.c`). A release installs when its notes' `commit-time:` is above 0 and above that image's floor; after the install, the floor is that commit time. A board without a key takes the first one a push sends, in the clear; replacing it needs phone setup. A Mac push never checks floors. A push or USB flash sets the floor so an older release can't replace it. A pushed launcher lands in factory and runs at the next power-on. CI skips a release whose inputs are unchanged, so commit times only move when code does.
- **Settings.** NVS namespace `cyd_config` is shared by every image: `wifi_ssid`, `wifi_pass`, `wildlife_url`, `ha_url`, `ha_token`, `ha_upstairs`, `ha_downstairs`, `ha_outside`, `ha_battery`, `ha_battery_pct`, `ha_wx_temp`, `ha_wx_high`, `ha_wx_low`, `ha_rain_today`, `ha_rain_week`, `ha_shed_pct`, `ha_shed_dawn`, `update_key`, `brightness`, `touch_cal`. No binary carries credentials. The launcher's network chooser and its phone setup write them. Phone setup (`launcher/main/phone_setup.c`) runs an open access point, `CYD-Setup-XXXX`, a DNS server that answers every name with the board, and one web page.
- **Display.** `cyd_display` reads the panel's RDID4 register (0xD3) at start-up and picks the 3.5" ST7796 or 2.8" ILI9341 profile, falling back to the 2.8" one. Screen size, font scale and text columns are runtime values, so file-scope arrays use `CYD_LCD_MAX_WIDTH`, `CYD_TEXT_MAX_COLUMNS` and the like. Text is a 10-row grid of 6x8 cells scaled 3x or 4x. Pixel data is sent column-major: the column address (0x2A) spans y.
- **Memory.** The boards have no PSRAM; about 320 KB of DRAM is free for data. Stream images through the drawing calls; a full 480x320 frame buffer doesn't fit.

## Hardware

The user has two 3.5" ESP32-3248S035R boards, verified on hardware with the launcher, phone setup and Wi-Fi. They also have a 2.8" ESP32-2432S028 with two USB ports, not yet tested; that revision may carry an ST7789, which doesn't answer RDID4 the same way and would need its own profile.
