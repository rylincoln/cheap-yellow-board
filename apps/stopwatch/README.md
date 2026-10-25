# ESP32 CYD Stopwatch

PlatformIO/ESP-IDF stopwatch built with [LVGL](https://lvgl.io) for the 320x240 or 480x320 display and resistive touchscreen on the ESP32 Cheap Yellow Display (CYD), following the [board documentation](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

It is one of the apps in this repository. The [root README](../../README.md) lists them all and the launcher that runs them.

## What it does

The watch face is a round dial with a tick mark for every second (long ones every 5) and an orange arc that sweeps around once a minute. The time is shown in the middle as `MM:SS.cc`, minutes, seconds and hundredths, switching to `H:MM:SS` after 100 minutes. A status line under it reads READY, RUNNING or STOPPED.

| Button | Action |
|---|---|
| **START** (green) | Starts, or resumes after STOP. Dimmed while running. |
| **STOP** (red) | Pauses, keeping the time. Dimmed while stopped. |
| **CLEAR** (gray) | Stops and resets to zero. |

Timing uses the ESP32's microsecond timer, so the display rate doesn't affect accuracy. The face updates about 30 times a second while running. Under the launcher, press EN to return to the boot menu.

## How LVGL is wired in

- **LVGL 9:** fetched at build time by the ESP-IDF component manager (`main/idf_component.yml`). Its options, such as fonts and widgets, are in `sdkconfig.defaults`.
- **Display:** LVGL renders 24-row strips in RGB565. The flush callback hands each strip to `cyd_draw_pattern` in `components/cyd_display`, which sends it in the column-major order this panel needs and keeps the saved launcher brightness.
- **Touch:** LVGL polls `cyd_touch_read`, which applies the calibration saved by `touchscreen_cal`. Pressing reads as LVGL's pressed state, so buttons show feedback while held.

## Build and flash

Install the PlatformIO IDE extension or PlatformIO Core. The first build installs the ESP32 platform and ESP-IDF toolchain and downloads LVGL. The project path must be short: builds fail on Windows when source paths pass 260 characters.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

The app binary is `%USERPROFILE%\.platformio\cyd-stopwatch-build\esp32dev\firmware.bin`. Use it in one of two ways:

- **As a launcher image:** every push to `main` runs the repository's `.github/workflows/build.yml`, which builds every app and publishes this one as `firmware.bin` on the release tagged `stopwatch`. The launcher lists that release and downloads the file. You can also write it to a slot over USB, as described in the [launcher README](../../launcher/README.md).
- **On its own:** `& $pio run --target upload --upload-port COM11`. This replaces everything on the board, including the launcher.

## Hardware

Both supported boards, the 2.8" ESP32-2432S028R and the 3.5" ESP32-3248S035R, are wired as listed in the [launcher README](../../launcher/README.md#hardware). `cyd_display` detects which one it is running on at start-up.
