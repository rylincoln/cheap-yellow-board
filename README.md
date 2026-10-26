# ESP32 Cheap Yellow Display apps

PlatformIO and ESP-IDF firmware, in C, for the ESP32 Cheap Yellow Display (CYD). A launcher turns the board into a two-slot boot menu that downloads apps from this repository's GitHub releases, and the apps are the things it boots.

Two boards are supported. `components/cyd_display` reads the panel's ID at start-up and picks the matching profile, so every binary runs on both:

| Board | Panel | Text grid |
|---|---|---|
| ESP32-2432S028R, 2.8" | ILI9341, 320x240 | 17x10 |
| ESP32-3248S035R, 3.5" | ST7796, 480x320 | 20x10 |

## What to expect

Flash the launcher once over USB; after that, new builds arrive over Wi-Fi. On every power-on it shows the apps stored in its two slots and counts down to the last one used. On the first start it asks for your Wi-Fi network instead, either on the touchscreen or from a phone through a temporary `CYD-Setup-XXXX` access point. It then lists the apps published on GitHub; tap one to download it into a slot and boot it. Brightness and Wi-Fi are set from the launcher, and every app uses them. Power the board off and on to get back to the launcher. The running app also installs new releases by itself, and `pio run -t upload` pushes a local build to it over the network, signed with an update key.

## Layout

| Folder | Contents |
|---|---|
| [`launcher/`](launcher/README.md) | The launcher, its custom second-stage bootloader and the partition table |
| [`apps/home_dashboard/`](apps/home_dashboard/README.md) | Home Assistant readings, with the latest bird on a tap |
| `components/cyd_display/` | Display, 5x7 font, patterns, touch and board detection, shared by every project |
| [`docs/`](docs/INSTALL.md) | Installing the tools, building, flashing and publishing |

Each project folder is a complete PlatformIO project; build it from that folder. Start with [docs/INSTALL.md](docs/INSTALL.md).

## Publishing

`.github/workflows/build.yml` builds the launcher and every app on each push and pull request. On `main` it publishes the launcher and each app as `firmware.bin` on a release tagged `launcher` or with the app's folder name, which is where the board finds them. An image whose inputs haven't changed isn't republished. A new folder under `apps/` is picked up by itself; its CMake `project()` name must match the folder name. Apps must fit a 1.31 MB slot and must never contain credentials, because their binaries are public.
