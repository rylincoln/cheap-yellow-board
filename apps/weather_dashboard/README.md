# ESP32 CYD Weather Dashboard

PlatformIO/ESP-IDF application for the 320x240 or 480x320 display and resistive touchscreen on the ESP32 Cheap Yellow Display (CYD). It is a second boot image, meant for a board that shows the weather outside and a shed battery. The [root README](../../README.md) describes the launcher that runs it.

## What it shows

The readings screen comes from Home Assistant and refreshes every 30 seconds:

- **Outside:** a large temperature, with the day's high (up triangle, amber) and low (down triangle, blue) under it, labeled `PAST 24 HOURS`.
- **Rain today** and **rain this week**, in inches. Dry reads `0.00`.
- **Shed:** one strip with the battery's charge in percent and a forecast of the charge at dawn.

Colors: the temperature is blue, the high amber, the low blue, rain light gray and the shed charge light gray; the dawn forecast is amber. Gray means no data: a reading that is missing shows `--` in gray, a dry rain tile is gray, and the temperature and the shed charge turn gray when Home Assistant hasn't heard from their sensor for 5 minutes. A charge, or a dawn forecast, of 20% and under turns red. A dawn forecast below zero means the battery runs out, so it reads `0` in red with an empty battery.

A tap on the readings does nothing. Until the first good reply, a status page shows the Wi-Fi network and the Home Assistant state; a tap on it asks Home Assistant again now. Its messages are `no HA address`, `no HA token`, `no sensor IDs`, `sensor IDs too long`, `HA 401 bad token`, `HA HTTP <code>` and `no sensors found`. If the network doesn't join, press EN to open the launcher and choose another. Without Wi-Fi settings, the screen says `NO WI-FI SETTINGS`.

## Settings

The app has no credentials compiled in, so its binary is safe to publish. It reads its settings at startup from the board's NVS, namespace `cyd_config`. The launcher's network chooser or its phone setup page writes them there:

| Key | Contents |
|---|---|
| `wifi_ssid`, `wifi_pass` | Wi-Fi network |
| `ha_url` | Home Assistant address |
| `ha_token` | A long-lived access token. It must belong to a Home Assistant admin user |
| `ha_wx_temp` | The outside temperature sensor, an entity ID such as `sensor.some_name` |
| `ha_wx_high`, `ha_wx_low` | The day's high and low, from two Statistics helpers |
| `ha_rain_today`, `ha_rain_week` | Rain so far today and this week, in inches |
| `ha_shed_pct` | The shed battery's charge, in percent |
| `ha_shed_dawn` | The forecast charge at dawn, in percent |

All seven sensor IDs are required; until they are set the status page says `no sensor IDs`.

Home Assistant has no sensor for the day's extremes, so make two Statistics helpers (Settings, Devices & services, Helpers) over the outside temperature sensor: one with the characteristic maximum and one with the minimum, each over 24 hours. Their IDs go in `ha_wx_high` and `ha_wx_low`. Until they exist the other readings show normally and the high and low show `--`.

Create the token at the bottom of the Home Assistant user's profile page, under Security, as a long-lived access token. The readings come from `POST /api/template`, which only admin users may call; a non-admin token shows `HA 401 bad token`. Home Assistant 2024.3 or newer is needed, since readings age by each sensor's `last_reported` time.

## Build and push

Install the PlatformIO IDE extension or PlatformIO Core. The project path must be short: builds fail on Windows when source paths pass 260 characters.

```sh
cd apps/weather_dashboard
USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run
```

On Windows run `pio run` from the PlatformIO IDE's own `pio.exe`. The app binary is `cyd-weather-build/esp32dev/firmware.bin` under `.platformio` in the user profile; the launcher's slot limit is 1,376,256 bytes.

- **As a launcher image:** every push to `main` runs the repository's `.github/workflows/build.yml`, which builds every app and publishes this one as `firmware.bin` on the release tagged `weather_dashboard`. The launcher lists that release and downloads the file.
- **Over the network:** `pio run -t upload` pushes it to the board while this app runs, signed with the update key, and the board restarts into it. See [docs/INSTALL.md](../../docs/INSTALL.md#7-put-builds-on-the-board). The app starts `cyd_update`, which keeps a new build only after 60 seconds and a drawn screen.

## Hardware

Both supported boards, the 2.8" ESP32-2432S028R and the 3.5" ESP32-3248S035R, are wired as listed in the [launcher README](../../launcher/README.md#hardware). `cyd_display` detects which one it is running on at start-up.
