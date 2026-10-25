# ESP32 CYD Screen Check

PlatformIO/ESP-IDF display test for the 320x240 or 480x320 screen and resistive touchscreen on the ESP32 Cheap Yellow Display (CYD), following the [board documentation](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

It is one of the apps in this repository. The [root README](../../README.md) lists them all and the launcher that runs them.

## What it does

Steps through full-screen test patterns. Tap the right half of the screen for the next pattern and the left half for the previous one. The list wraps around. Each pattern's name shows on the bottom row for 1.5 seconds, then disappears so it doesn't get in the way of the test. Under the launcher, press EN to return to the boot menu.

| Pattern | What to look for |
|---|---|
| Red, green, blue, white, black, cyan, magenta, yellow, 50% gray | Dead or stuck pixels, uneven backlight, tint |
| Color bars | Correct color order (white, yellow, cyan, green, magenta, red, blue, black); swapped red and blue |
| RGB ramps | Smooth red, green, blue and gray gradients; banding |
| Gray steps | 16 distinguishable steps from black to white |
| Checkerboard | 1-pixel alternation; flicker or crosstalk |
| Grid | Straight 40-pixel grid lines and a 1-pixel red border on all four edges; clipping or offset |

It needs no Wi-Fi or credentials. Patterns are drawn pixel by pixel through `cyd_draw_pattern` in `components/cyd_display`, without a frame buffer.

## Build and flash

Install the PlatformIO IDE extension or PlatformIO Core. The first build installs the ESP32 platform and ESP-IDF toolchain. The project path must be short: builds fail on Windows when source paths pass 260 characters.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

The app binary is `%USERPROFILE%\.platformio\cyd-screen-check-build\esp32dev\firmware.bin`, about 180 KB. Use it in one of two ways:

- **As a launcher image:** every push to `main` runs the repository's `.github/workflows/build.yml`, which builds every app and publishes this one as `firmware.bin` on the release tagged `screen_check`. The launcher lists that release and downloads the file. You can also write it to a slot over USB, as described in the [launcher README](../../launcher/README.md).
- **On its own:** `& $pio run --target upload --upload-port COM11`. This replaces everything on the board, including the launcher.

Monitor with `& $pio device monitor --port COM11 --baud 115200`. Ctrl+] exits.

## Hardware

Both supported boards, the 2.8" ESP32-2432S028R and the 3.5" ESP32-3248S035R, are wired as listed in the [launcher README](../../launcher/README.md#hardware). `cyd_display` detects which one it is running on at start-up.
