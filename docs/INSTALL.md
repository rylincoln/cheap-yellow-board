# Build Environment and Installation

How to set up a PC from scratch to build every application in this repository, flash an ESP32 Cheap Yellow Display (CYD), and publish new boot images. Everything here was used and verified on the machine that built this project. Exact versions are listed at the end.

## At a glance

| You need | Required? | Why |
|---|---|---|
| A **CYD board** (2.8" ESP32-2432S028R or 3.5" ESP32-3248S035R) and a USB data cable | Yes | The hardware. It shows up as a USB serial port. |
| **USB-serial driver** (CH340) | Usually | Windows needs it to see the board's COM port. Some boards use a CP2102 instead. |
| **Git** | Yes | To get the source. |
| **PlatformIO Core** (`pio`) | Yes | Builds everything. It downloads ESP-IDF, the compiler and esptool itself. |
| **Python 3** | Indirectly | PlatformIO runs on Python. Its installers set up their own environment. |
| **Visual Studio Code** + PlatformIO IDE extension | **No**, optional | The easiest way to install PlatformIO, plus an editor and build buttons. Every build and flash in this project also works from a terminal. |
| ESP-IDF, compilers, CMake, esptool | No separate install | PlatformIO fetches them automatically on the first build. |
| A Wi-Fi network | For downloads | Set on the board, with the launcher's network chooser or from a phone. No binary has credentials compiled in. |
| A GitHub account with push access | To publish images | GitHub Actions builds the apps and publishes their binaries. |
| GitHub CLI (`gh`) | No | Not used. CI uses it on GitHub's side only. |

**High level:** install Git, install PlatformIO (through VS Code or on its own), clone the repository to a short path, then build `launcher/` and flash it once with esptool. On its first start the launcher asks for your Wi-Fi network, on its screen or from a phone, and then downloads every app from GitHub by itself. You only build an app locally when you change it.

## 1. Install the USB driver and find the board's port

1. Plug the CYD in with a data cable. Some cables only carry power.
2. Open **Device Manager → Ports (COM & LPT)**. You should see a port such as **USB-SERIAL CH340 (COM11)**. The number varies by PC; use yours wherever these docs say `COM11`.
3. If nothing appears, or the device has a warning icon, install the **CH340/CH341** driver from the chip maker (WCH). If your board has a Silicon Labs chip instead, install the **CP210x** driver.

## 2. Install Git

Install [Git for Windows](https://git-scm.com/download/win) with the default options. Then, in a new terminal:

```bash
git config --global core.longpaths true
```

The repository uses short paths, but Git's own internals can still pass 260 characters.

## 3. Install PlatformIO: pick one

### Option A: VS Code and the PlatformIO IDE extension (recommended for most people)

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. In VS Code, open **Extensions**, search for **PlatformIO IDE**, and install it. On first start it installs **PlatformIO Core** into `%USERPROFILE%\.platformio\penv`. This takes a few minutes, and it finds or downloads a suitable Python itself.
3. When you open the repository, or a project folder in it, VS Code recommends this extension, from `.vscode/extensions.json`. That file also tells VS Code *not* to suggest the C/C++ extension pack or `pioarduino`, which would conflict with PlatformIO's own setup.

The `pio` command lives at `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. The docs call it by that full path, so it works even when `pio` isn't on your PATH. To type plain `pio` instead, add `%USERPROFILE%\.platformio\penv\Scripts` to your user PATH. The VS Code tasks in `.vscode/tasks.json` (Build, Upload, Monitor) assume `pio` is on the PATH.

### Option B: PlatformIO Core on its own (no VS Code)

1. Install [Python 3](https://www.python.org/downloads/) (3.10 or newer; 3.13 is used here, 3.12 in CI). Tick **Add python.exe to PATH**.
2. Install PlatformIO Core:

   ```bash
   python -m pip install --user platformio
   ```

   Alternatively, use PlatformIO's official installer script, which creates the same `%USERPROFILE%\.platformio\penv` environment as the VS Code extension. Then the paths in these docs work unchanged.

Either way, check it:

```bash
pio --version
```

Version 6.2.0 is used here. Any 6.x should work.

## 4. Get the source

Clone to a **short path**. The ESP-IDF build fails on Windows when a source path passes 260 characters, and Windows long-path support is not required if the path stays short. `C:\Users\<you>\Documents\cheap-yellow-board` works. Avoid deeply nested folders and app-virtualized locations under `AppData`.

```bash
git clone https://github.com/rylincoln/cheap-yellow-board.git
cd cheap-yellow-board
```

The layout:

| Folder | Contents | Published by CI |
|---|---|---|
| `launcher/` | Launcher (factory app), custom second-stage bootloader, partition table | No: flash it over USB once |
| `apps/birdnet_viewer/` | BirdNET bird photo viewer | Yes |
| `components/cyd_display/` | Shared display and touch driver, with board detection | Built into every project |
| `docs/` | This guide | No |

`launcher/` and each folder under `apps/` is a complete PlatformIO project. They all build the one shared driver, through `EXTRA_COMPONENT_DIRS` in their `CMakeLists.txt`.

## 5. Wi-Fi settings live on the board

There is nothing to create before building. The first time the launcher starts, it finds no saved network and opens its Wi-Fi chooser. Pick a network and type its password on the screen, or tap `PHONE SETUP` and do it from a phone: the board becomes a temporary access point named `CYD-Setup-XXXX` with a setup page that also takes the BirdNET server. The launcher saves the settings in the board's NVS storage, where every app reads them.

## 6. Build

Each project builds the same way, from its own folder:

```bash
cd launcher        # or apps/<name>
"$USERPROFILE/.platformio/penv/Scripts/pio.exe" run
```

In PowerShell:

```powershell
cd launcher
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

On macOS or Linux, set `USERPROFILE` for the build folder in `platformio.ini`: `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run`.

What to expect:

- **First build on a new PC:** PlatformIO downloads the pinned `espressif32@7.1.3` platform, with ESP-IDF 6.1.0, the Xtensa GCC 15.2 toolchain, CMake, Ninja and esptool. That's a large download, and it lands in `%USERPROFILE%\.platformio`.
- **First build of each project:** about 7 to 17 minutes, because each project has its own build folder (`%USERPROFILE%\.platformio\cyd-*-build`) and compiles all of ESP-IDF. Later builds take seconds to a couple of minutes.
- **Downloaded components** (`espressif/qrcode` for the launcher) land in that project's `managed_components/`, which is git-ignored, at the exact version in its `dependencies.lock`.
- **Output:** `firmware.bin` in that project's build folder. The launcher also produces `bootloader.bin`, `partitions.bin` and `ota_data_initial.bin`.
- **After editing `sdkconfig.defaults`:** delete the generated `sdkconfig.esp32dev` in that project's folder, so the new defaults apply. Expect a full rebuild.

## 7. Flash a new board (once)

The launcher setup needs its own bootloader, partition table and a blank `otadata`. Flashing all of them explicitly with **esptool**, at these offsets, is the verified way. Don't use `pio run --target upload` in `launcher/`: it writes a stock bootloader. This erases the whole board:

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$L = "$env:USERPROFILE\.platformio\cyd-launcher-build\esp32dev"
& $py $esptool --chip esp32 -p COM11 erase_flash
& $py $esptool --chip esp32 -p COM11 -b 230400 write_flash `
    0x1000 "$L\bootloader.bin" 0x8000 "$L\partitions.bin" `
    0xd000 "$L\ota_data_initial.bin" 0x10000 "$L\firmware.bin"
```

Power-cycle the board. The launcher asks for your Wi-Fi network, then lists every app that has a published build and downloads the one you tap into one of its two slots. To watch the serial log:

```powershell
& $pio device monitor --port COM11 --baud 115200
```

Ctrl+] exits.

To put a locally built app into a slot without GitHub, write its `firmware.bin` to `0x150000` (slot A) or `0x2A0000` (slot B):

```powershell
& $py $esptool --chip esp32 -p COM11 -b 230400 write_flash 0x150000 path\to\firmware.bin
```

An app can also run on its own, without the launcher: `& $pio run --target upload --upload-port COM11` in its folder. This replaces everything on the board, including the launcher.

## 8. Publish a new or changed application

Push to `main`. The repository's one workflow, `.github/workflows/build.yml`, builds the launcher and every folder under `apps/` on GitHub Actions, on every push and pull request. On `main` it also uploads each app as `firmware.bin` to the release tagged with the app's folder name, creating the release the first time. On its next start, the launcher shows the new build as an update.

To add a new application, copy an existing app folder (for example `apps/birdnet_viewer`), set `project(...)` in its `CMakeLists.txt` to the new folder name and change `build_dir` in `platformio.ini`, write the app in `main/`, and push. The workflow picks up the new folder by itself. The project name must match the folder name, or the launcher treats a downloaded copy as flashed over USB. Boot images must stay under 1.31 MB, the slot size, and must never contain credentials, because their binaries are public.

The workflow needs the repository's Actions to have **read and write** permission for contents, so it can create releases. It asks for this with `permissions: contents: write`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `cc1.exe: fatal error: /?/C:/...` or other path errors | The project path is too long. Clone to a shorter folder. |
| esptool: `The chip stopped responding` | Retry at a lower baud rate, such as `-b 115200`. Use a short, good USB cable. |
| esptool can't open the port | Close any serial monitor, or other program, using the COM port. |
| A `sdkconfig.defaults` change has no effect | Delete `sdkconfig.esp32dev` and rebuild. |
| CI build fails on size | The workflow checks against the default 1 MB app partition. Keep standalone builds under 1 MB, or point the check at the launcher's partition table. |
| Touches land in the wrong place | The built-in calibration is used, and a calibration saved earlier is still applied. |

## Tested versions

| Component | Version |
|---|---|
| Windows | 10 Pro 22H2 (build 19045), long-path support **off** |
| Git | 2.55.0.windows.5 |
| Visual Studio Code extension | PlatformIO IDE 3.3.4 |
| PlatformIO Core | 6.2.0 (Python 3.13.0 in its `penv`) |
| PlatformIO platform | espressif32 7.1.3 (pinned in every `platformio.ini`) |
| ESP-IDF | 6.1.0 (`framework-espidf` 4.60100.0) |
| Compiler | `toolchain-xtensa-esp-elf` 15.2.0+20251204 |
| CMake / Ninja | 3.30.2 / 1.9.0 (PlatformIO packages) |
| esptool | 4.11.0 |
| USB-serial chip | WCH CH340 (USB ID 1A86:7523) |
| CI | GitHub Actions `ubuntu-latest`, Python 3.12, PlatformIO from `pip` |
