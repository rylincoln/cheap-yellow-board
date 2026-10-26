# ESP32 CYD Bootloader and Launcher

PlatformIO/ESP-IDF firmware for the ESP32 Cheap Yellow Display (CYD), following the [board documentation](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display). It turns the board into a two-slot boot menu.

This folder holds the launcher, its custom second-stage bootloader and the partition table. The apps it boots are the folders under `apps/`, listed in the [root README](../README.md).

Every app builds a binary that runs in either launcher slot. They all use one copy of `components/cyd_display` (display, 5x7 font, patterns and touch) at the repository root. It reads the panel's ID at start-up and picks the matching board profile, so the same binary runs on the 2.8" and 3.5" boards listed under [Hardware](#hardware).

## How it boots

On power-on, including the EN button, the custom bootloader (`bootloader_components/main`) starts the launcher, or the chosen image if the launcher's image doesn't verify. Any other reset, such as the launcher's own restart or an image crashing, goes straight back to the chosen image.

1. On the very first start, or whenever no network is saved, the Wi-Fi network chooser opens first (see below); the menu and its countdown follow once it closes.
2. The menu immediately lists the stored images, marking the default with `*`, and counts down from 20 seconds. It shows seven entries at a time: the row below them says which (such as `1-7/9`), and its ▲ and ▼ buttons page through the rest.
3. When the countdown ends, the last-used image boots. The very first time, it is the first stored image.
4. Tapping anywhere except an image or the network activity, the scroll buttons, brightness slider and the bottom row's other pages included, adds another 20 seconds. Tap an image to boot it right away.
   - The brightness slider runs down the right edge: `+` at the top, a bar with one segment per level, `-` below it, then the level in percent. `+` and `-` step the backlight by 20%, between 20% and 100%, and tapping a bar segment jumps to its level. The level is saved in NVS (`cyd_config/brightness`). Every image starts at that level, because the shared display driver reads it.
   - The bottom row shows what the launcher is doing, taking turns every 2.5 seconds (tap it to skip ahead): the network activity (`join <network>`, why a join failed such as `no AP found` or `bad password?`, `GET releases`, `7 on GitHub`, or a GitHub error such as `GitHub err 403`), then, once joined, the IP address and the signal strength with the network name, then the free RAM. New network activity shows at once.
   - Tapping the bottom row while it shows the network activity opens the Wi-Fi network chooser, and the countdown waits until it closes, then starts over from 20 seconds. The chooser scans once any join in progress has finished, then lists the networks in range, strongest first. Each name appears once, with its strongest access point, and the joined network is in yellow. It pages five at a time, `SCAN` scans again, and `BACK` returns to the menu. Tapping a network shows its channel, signal, security and address; `CONNECT` opens an on-screen keyboard with the name and password (filled in if it is the saved network; type the name of a hidden network). `SAVE` stores them in `cyd_config` for the launcher and every image. The launcher then rejoins, with progress on the bottom row, and lists the GitHub releases again.
   - Below the networks, `PREFER PHONE?` and `PHONE SETUP` offer the same settings from a phone. Tapping either starts phone setup: the board also becomes an open Wi-Fi access point named `CYD-Setup-` plus the last four hex digits of its address, and the screen says how to reach it. `QR CODE` shows a code that joins a phone to it. Phones open the setup page by themselves when they join, because the board answers every DNS name with its own address and redirects their connectivity checks to the page; otherwise browse to `192.168.4.1`. The page lists the networks from the last scan and has fields for the network, password, `Wildlife camera server` and Home Assistant (address, token and four sensor IDs). It never shows the saved password or token: leaving the password blank keeps it when the network stays the same, and otherwise means an open network; leaving the token blank keeps it when the Home Assistant address stays the same. Saving stores the settings in `cyd_config`, shows `SAVED BY PHONE` on the board, stops the access point and rejoins as above. `BACK` stops the access point without saving.
5. Meanwhile the launcher joins Wi-Fi and lists the releases of `rylincoln/cheap-yellow-board` (`IMAGE_REPO` in `main/main.c`; override it with a `-DIMAGE_REPO` build flag). Every release with a `firmware.bin` asset, up to 16, is an app named after its tag, which CI sets to the app's folder name. This usually takes a few seconds. Each app whose current build isn't stored appears in cyan as `+ name`: either a new app or a newer build of a stored one. Tapping one downloads it into a slot, with progress, and boots it. A release without `firmware.bin` is skipped and logged. If Wi-Fi or GitHub doesn't answer, the bottom row says why and nothing else changes; tap it to choose another network.

A download replaces the slot holding the same app if there is one. Otherwise it fills an empty slot, or overwrites the slot that does not boot by default. Each slot remembers its app's name and the asset's `updated_at` time in NVS, so a rebuilt app shows up again as an update. A slot whose image's CMake project name differs from the name it was downloaded as counts as flashed over USB, so each app's `project()` name must match its folder.

### Settings for boot images

No binary carries credentials, the launcher included. The launcher and every boot image read their settings from NVS namespace `cyd_config`, which is set on the device and survives restarts and new builds. Erasing the flash clears it, and the next start opens the network chooser again.

| Key | Set by |
|---|---|
| `wifi_ssid`, `wifi_pass` | The launcher's network chooser or phone setup |
| `wildlife_url` | Phone setup: the wildlife camera gallery, such as `http://192.168.1.50:8080` |
| `update_key` | Phone setup's Updates field, or the first push to a board without one: the key `tools/cyd_push.py --new-key` makes, which signs pushes from the Mac |
| `ha_url`, `ha_token` | Phone setup: the Home Assistant address and a long-lived access token from an admin user |
| `ha_upstairs`, `ha_downstairs`, `ha_outside`, `ha_battery` | Phone setup: the four sensor entity IDs, such as `sensor.some_name` |

To update the launcher without losing these settings, write only its `firmware.bin` to `0x10000` with esptool and do not erase the flash.

Flash layout (4 MB, `partitions.csv`):

| Partition | Offset | Size | Contents |
|---|---|---|---|
| `nvs` | 0x9000 | 16 KB | Slot apps and versions; settings for boot images; Wi-Fi data |
| `otadata` | 0xd000 | 8 KB | Last-chosen image |
| `factory` | 0x10000 | 1.25 MB | Launcher (about 1.1 MB) |
| `ota_0` | 0x150000 | 1.31 MB | Boot image slot A |
| `ota_1` | 0x2A0000 | 1.31 MB | Boot image slot B |

## Publishing a boot image

The repository's single workflow, `.github/workflows/build.yml`, builds this launcher and every app on each push. On `main` it publishes each app as `firmware.bin` on a release tagged with the app's folder name, which is what the launcher lists. The launcher is published too, on the `launcher` release, and a running app installs it, to take effect at the next power-on; its bootloader and partition table still go over USB, as below. `pio run -t upload` pushes a build to the board over the network; see [docs/INSTALL.md](../docs/INSTALL.md#7-put-builds-on-the-board).

## Build and flash

Install the PlatformIO IDE extension or PlatformIO Core. The first build installs the ESP32 platform and ESP-IDF toolchain. The project path must be short: builds fail on Windows when source paths pass 260 characters.

The first build also downloads the `espressif/qrcode` component (pinned in `main/idf_component.yml` and `dependencies.lock`) into `managed_components/`.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

`pio run --target upload` pushes over the network and never writes the bootloader. To flash over USB, use esptool. This erases the board, including both image slots:

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$L = "$env:USERPROFILE\.platformio\cyd-launcher-build\esp32dev"
& $py $esptool --chip esp32 -p COM11 erase_flash
& $py $esptool --chip esp32 -p COM11 -b 230400 write_flash `
    0x1000 "$L\bootloader.bin" 0x8000 "$L\partitions.bin" `
    0xd000 "$L\ota_data_initial.bin" 0x10000 "$L\firmware.bin"
& $pio device monitor --port COM11 --baud 115200
```

To put an application into a slot over USB, build it in its folder under `apps/` and write its `firmware.bin` to `0x150000` (slot A) or `0x2A0000` (slot B):

```powershell
& $py $esptool --chip esp32 -p COM11 -b 230400 write_flash 0x150000 path\to\firmware.bin
```

Change `COM11` to the board's serial port. The monitor exits with Ctrl+]. If you change `sdkconfig.defaults`, delete the generated `sdkconfig.esp32dev` so the new defaults apply.

## Hardware

`cyd_display` reads the panel's ID register (RDID4, 0xD3) over the display bus at start-up and picks the board profile with that ID. An unknown ID falls back to the 2.8" board.

| | ESP32-2432S028R (2.8") | ESP32-3248S035R (3.5") |
|---|---|---|
| Panel | ILI9341, 320x240 | ST7796, 480x320 |
| Text grid | 17x10 cells, font at 3x | 20x10 cells, font at 4x |
| Display bus | MOSI GPIO13, MISO GPIO12, SCLK GPIO14, CS GPIO15, D/C GPIO2 | Same |
| Backlight | GPIO21 | GPIO27 |
| Touch (XPT2046) | Own bus: MOSI GPIO32, MISO GPIO39, SCLK GPIO25, CS GPIO33, IRQ GPIO36 | Shares the display bus; CS GPIO33, IRQ GPIO36 |

The backlight is PWM-dimmed to the brightness saved by the launcher. The panel fills a drawing window column by column, so pixel data is sent column-major. Each profile carries a built-in touch calibration, which a calibration saved in `cyd_config/touch_cal` replaces.
