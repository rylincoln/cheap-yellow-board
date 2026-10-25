# ESP32 CYD BirdNET Viewer

PlatformIO/ESP-IDF application for the 320x240 or 480x320 display and resistive touchscreen on the ESP32 Cheap Yellow Display (CYD), following the [board documentation](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

It is one of the apps in this repository. The [root README](../../README.md) lists them all and the launcher that runs them.

## What it does

The viewer connects to Wi-Fi and queries a local BirdNET server every two minutes. The latest visit from today with a confidence of at least 80% is shown as a full-screen Wikimedia photo, with no text. A failed refresh keeps the last photo.

Tap the photo for the bird's details: its name and scientific name, its family (from [GBIF](https://www.gbif.org/developer/species), since the BirdNET server has no taxonomy), when it was heard (`10-20 09:06-09:13`), the number of calls and the best confidence, the current temperature and sky (the server keeps only the current weather, not the weather at the time of a detection), and how often and since when the station has heard the species. The details fill in as they arrive. Tap again, or wait 30 seconds, to go back to the photo, which is fetched again because it is streamed rather than kept in RAM.

Until a photo is up, and whenever none is, a status screen shows why the viewer started (marking a crash, watchdog or brownout reset, which boots straight back into the viewer, with what it was doing), the Wi-Fi network and why joining fails (such as `#12 no AP found`), the BirdNET server and its last request (such as `no bird >= 80%` or `visits HTTP 404`), and the free RAM. If the network doesn't join, press EN to open the launcher and choose another. A tap on the status screen refreshes at once.

It started as the original single-app viewer, then moved to the shared display component (`components/cyd_display`). That port fixes swapped red and blue and transposed image tiles on this panel.

The viewer has no credentials compiled in, so its binary is safe to publish. It reads its settings at startup from the board's NVS, namespace `cyd_config`. The launcher's network chooser or its phone setup page writes them there:

| Key | Contents |
|---|---|
| `wifi_ssid`, `wifi_pass` | Wi-Fi network |
| `birdnet_url` | BirdNET server, for example `http://192.168.1.50`; defaults to `http://birdnet.local` |

Without Wi-Fi settings, the screen says so. Press EN to open the launcher, which starts its Wi-Fi setup when no network is saved, then boot the viewer again. The viewer uses:

- `GET {BIRDNET_BASE_URL}/api/v1/detections/visits`
- `GET {BIRDNET_BASE_URL}/api/v1/image/{URL-encoded-scientific-name}`
- For the details: `GET {BIRDNET_BASE_URL}/api/v1/weather/current`, `GET {BIRDNET_BASE_URL}/api/v1/species/detail?sci_name=...` and `GET https://api.gbif.org/v1/species/match?name=...`

Wikimedia's 1280-pixel thumbnail is streamed over HTTPS straight into the ESP32 ROM JPEG decoder and scaled down to fit the panel, so the full image is never held in RAM.

## Build and flash

Install the PlatformIO IDE extension or PlatformIO Core. The first build installs the ESP32 platform and ESP-IDF toolchain. The project path must be short: builds fail on Windows when source paths pass 260 characters.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

The app binary is `%USERPROFILE%\.platformio\cyd-birdnet-build\esp32dev\firmware.bin`, about 1.0 MB. Use it in one of two ways:

- **As a launcher image:** every push to `main` runs the repository's `.github/workflows/build.yml`, which builds every app and publishes this one as `firmware.bin` on the release tagged `birdnet_viewer`. The launcher lists that release and downloads the file. You can also write it to a slot over USB, as described in the [launcher README](../../launcher/README.md).
- **On its own:** `& $pio run --target upload --upload-port COM11`. This replaces everything on the board, including the launcher and the settings it stored. The viewer then has no Wi-Fi settings.

Monitor with `& $pio device monitor --port COM11 --baud 115200`. Ctrl+] exits.

## Hardware

Both supported boards, the 2.8" ESP32-2432S028R and the 3.5" ESP32-3248S035R, are wired as listed in the [launcher README](../../launcher/README.md#hardware). `cyd_display` detects which one it is running on at start-up.
