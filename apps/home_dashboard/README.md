# ESP32 CYD Home Dashboard

PlatformIO/ESP-IDF application for the 320x240 or 480x320 display and resistive touchscreen on the ESP32 Cheap Yellow Display (CYD), following the [board documentation](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

It is the only app in this repository so far. The [root README](../../README.md) describes the launcher that runs it.

## What it does

The glance screen is the default. It shows big readings from Home Assistant: Upstairs and Downstairs stacked on the left, a large Outside on the right, and a strip along the bottom with the shop battery's current and charge. The readings refresh every 30 seconds. A reading turns gray when Home Assistant hasn't heard from its sensor for 5 minutes. In the battery strip, a red down arrow means draining, a green bolt means charging, and a gray current with neither means idle, under 0.1 A. The battery icon beside it fills to the charge, and the charge turns red at 20% and under. Without `ha_battery_pct` the strip shows only the current.

Until the first good reply, a status page shows the Wi-Fi network and the Home Assistant state. Its messages are `no HA address`, `no HA token`, `no sensor IDs`, `HA 401 bad token`, `HA HTTP <code>` and `no sensors found`. If the network doesn't join, press EN to open the launcher and choose another.

### Birds on tap

Tap the readings to see the latest bird from your wildlife camera gallery, which identifies birds by their song. The newest bird's species photo fills the screen, with its aspect kept. Tap again for its details: name, time, confidence and `also heard` with the next birds. Tap once more to return to the readings. Without a tap, the photo or details return to the readings after 30 seconds.

The messages are `NO WI-FI`, `NO WILDLIFE CAM / use phone setup`, `NO BIRDS YET` and `WILDLIFE ERROR / <reason>`.

## Settings

The app has no credentials compiled in, so its binary is safe to publish. It reads its settings at startup from the board's NVS, namespace `cyd_config`. The launcher's network chooser or its phone setup page writes them there:

| Key | Contents |
|---|---|
| `wifi_ssid`, `wifi_pass` | Wi-Fi network |
| `ha_url` | Home Assistant address, for example `http://homeassistant.local:8123` |
| `ha_token` | A long-lived access token. It must belong to a Home Assistant admin user |
| `ha_upstairs`, `ha_downstairs`, `ha_outside`, `ha_battery` | Sensor entity IDs, such as `sensor.some_name`. `ha_battery` is the battery's current in amps |
| `ha_battery_pct` | Optional: the battery's charge sensor, in percent |
| `wildlife_url` | Wildlife camera gallery, for example `http://192.168.1.50:8080` |

Create the token at the bottom of the Home Assistant user's profile page, under Security, as a long-lived access token. The readings come from `POST /api/template`, which only admin users may call. A non-admin token shows `HA 401 bad token`, even though it can read `/api/states`. The token is therefore a full admin credential, sent in cleartext unless `ha_url` is `https`, so consider a dedicated admin user for the board.

Home Assistant 2024.3 or newer is needed: the readings age by each sensor's `last_reported` time.

`wildlife_url` replaces the old BirdNET server setting; the launcher erases that key when the phone page saves.

An older launcher's phone page lacks the Home Assistant and wildlife camera fields. Flash the current launcher first: write only its `firmware.bin` to `0x10000`, without erasing, as the [launcher README](../../launcher/README.md#settings-for-boot-images) describes.

Without Wi-Fi settings, the screen says so. Press EN to open the launcher, which starts its Wi-Fi setup when no network is saved, then boot the app again. The app uses:

- `POST {ha_url}/api/template`, every 30 seconds, with the token as a bearer token
- `GET {wildlife_url}/frame/wildlife`, when the readings are tapped

## Build and flash

Install the PlatformIO IDE extension or PlatformIO Core. The first build installs the ESP32 platform and ESP-IDF toolchain. The project path must be short: builds fail on Windows when source paths pass 260 characters.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

The app binary is `%USERPROFILE%\.platformio\cyd-dashboard-build\esp32dev\firmware.bin`, about 1.27 MB; the launcher's slot limit is 1,376,256 bytes. Use it in one of two ways:

- **As a launcher image:** every push to `main` runs the repository's `.github/workflows/build.yml`, which builds every app and publishes this one as `firmware.bin` on the release tagged `home_dashboard`. The launcher lists that release and downloads the file. You can also write it to a slot over USB, as described in the [launcher README](../../launcher/README.md).
- **Over the network:** `pio run -t upload` pushes it to the board while this app runs, signed with the update key, and the board restarts into it. See [docs/INSTALL.md](../../docs/INSTALL.md#7-put-builds-on-the-board). The app starts `cyd_update`, which keeps a new build only after 60 seconds and a drawn screen.

Monitor with `& $pio device monitor --port COM11 --baud 115200`. Ctrl+] exits.

## Hardware

Both supported boards, the 2.8" ESP32-2432S028R and the 3.5" ESP32-3248S035R, are wired as listed in the [launcher README](../../launcher/README.md#hardware). `cyd_display` detects which one it is running on at start-up.
