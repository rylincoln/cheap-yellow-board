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

**High level:** install Git, install PlatformIO (through VS Code or on its own), clone the repository to a short path, then build `launcher/` and flash it once with esptool. After that, new builds reach the board over Wi-Fi. On its first start the launcher asks for your Wi-Fi network, on its screen or from a phone, and then downloads every app from GitHub by itself. You only build an app locally when you change it.

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

The `pio` command lives at `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. The docs call it by that full path, so it works even when `pio` isn't on your PATH. To type plain `pio` instead, add `%USERPROFILE%\.platformio\penv\Scripts` to your user PATH.

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
| `launcher/` | Launcher (factory app), custom second-stage bootloader, partition table | Yes, as `firmware.bin` on the release tagged `launcher` |
| `tools/` | `cyd_push.py`, which `pio run -t upload` uses to push a build over the network | No |
| `apps/home_dashboard/` | Home Assistant readings, with the latest bird on a tap | Yes |
| `components/cyd_display/` | Shared display and touch driver, with board detection | Built into every project |
| `docs/` | This guide | No |

`launcher/` and each folder under `apps/` is a complete PlatformIO project. They all build the one shared driver, through `EXTRA_COMPONENT_DIRS` in their `CMakeLists.txt`.

## 5. Wi-Fi settings live on the board

There is nothing to create before building. The first time the launcher starts, it finds no saved network and opens its Wi-Fi chooser. Pick a network and type its password on the screen, or tap `PHONE SETUP` and do it from a phone: the board becomes a temporary access point named `CYD-Setup-XXXX` with a setup page that also takes the Home Assistant settings and the `Wildlife camera server`. The launcher saves the settings in the board's NVS storage, where every app reads them.

The setup page also has an **Updates** field for the **update key**, which lets your Mac push builds to the board. The first push from the Mac gives a board that has no key its key (see section 7), so this field is for setting one by hand or changing it. The board keeps it normalized, and a blank field keeps the saved key. A board without a key refuses every push until the first one gives it a key.

The home dashboard needs:

- **Home Assistant address and token.** For example `http://homeassistant.local:8123`. Create the token at the bottom of your Home Assistant user's profile page, under Security, as a long-lived access token. The token must belong to an admin user: the dashboard reads through `POST /api/template`, which is admin-only, and a non-admin token shows `HA 401 bad token`. That makes it a full admin credential, sent in cleartext unless the address is `https`, so consider a dedicated admin user for the board. Home Assistant 2024.3 or newer is needed.
- **Sensor IDs.** The entity IDs for upstairs, downstairs, outside and the battery current, such as `sensor.some_name`, and optionally the battery's charge in percent.
- **Wildlife camera server.** The address of your wildlife-cam gallery, for example `http://192.168.1.50:8080`. Tapping the readings shows its latest bird.

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

The pure C modules also have host tests. From the repository root, `sh test/run.sh` builds and runs them with the system C compiler (`cc`); on Windows, use WSL or another POSIX shell with a compiler. CI runs them before it builds anything.

What to expect:

- **First build on a new PC:** PlatformIO downloads the pinned `espressif32@7.1.3` platform, with ESP-IDF 6.1.0, the Xtensa GCC 15.2 toolchain, CMake, Ninja and esptool. That's a large download, and it lands in `%USERPROFILE%\.platformio`.
- **First build of each project:** about 7 to 17 minutes, because each project has its own build folder (`%USERPROFILE%\.platformio\cyd-*-build`) and compiles all of ESP-IDF. Later builds take seconds to a couple of minutes.
- **Downloaded components** (`espressif/qrcode` for the launcher) land in that project's `managed_components/`, which is git-ignored, at the exact version in its `dependencies.lock`.
- **Output:** `firmware.bin` in that project's build folder. The launcher also produces `bootloader.bin`, `partitions.bin` and `ota_data_initial.bin`.
- **After editing `sdkconfig.defaults`:** delete the generated `sdkconfig.esp32dev` in that project's folder, so the new defaults apply. Expect a full rebuild.
- **After adding source files or an `idf_component.yml`** under the shared `components/` folder: also delete the project's `sdkconfig.esp32dev`. PlatformIO re-runs CMake only when the project's own `CMakeLists.txt` or `sdkconfig` changes, so without this the new files aren't built.
- **After changing a bootloader setting** in `launcher/sdkconfig.defaults`: delete `sdkconfig.esp32dev`, and also the `bootloader` folder inside the build directory (`%USERPROFILE%\.platformio\cyd-launcher-build\esp32dev\bootloader`). The bootloader's own sub-build doesn't pick up a regenerated `sdkconfig.esp32dev`.

## 7. Put builds on the board

There are two ways: over Wi-Fi, which is the everyday route, and over USB with esptool, which is for a new board or for recovery.

### Over the network

`pio run -t upload`, run in `launcher/` or an app folder, sends the built `firmware.bin` to the board over HTTP. The script is `tools/cyd_push.py`. It needs Python 3.9 or newer and `curl`, and it signs each push with the update key, so the key crosses the network only once: when the first push gives it to a board that has none. Three things must be in place:

1. **The board's address.** The board announces itself as `cyd-xxxx.local`, where `xxxx` is the last four hex digits of its Wi-Fi MAC address; the launcher's bottom row shows its IP address too. There is no default. Set `CYD_HOST` (for example `export CYD_HOST=cyd-xxxx.local`), or put the address on the first line of `~/.config/cyd/host`.
2. **The update key.** Run `python3 tools/cyd_push.py --new-key` once. It saves a key to `~/.config/cyd/update_key` with mode 600, or to the file `CYD_KEY_FILE` names, and on macOS copies it to the clipboard with `pbcopy`. It refuses to overwrite an existing key unless you add `--force`. The first push sends it to a board that has no key, over plain HTTP, so anything on your network could give such a board a key first. A board that has a key refuses a new one over the network; change it on the phone setup page. The script refuses a key file that others can read.
3. **A board running an app that has `cyd_update`.** The running app takes the push, not the launcher's menu, so let the launcher's countdown boot the app first. A board with older images needs the one-time USB step below.

```bash
cd apps/home_dashboard
USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run -t upload
```

The board shows `UPDATING`, a percentage, then `RESTARTING`, and the new build starts. A pushed launcher image (project `cyd_launcher`) is written to the factory partition while an app runs, and takes effect at the next power-on; the board shows `LAUNCHER UPDATED`. If the board answers `busy <seconds>`, the script waits and tries again, up to three times. The board takes one push at a time, so a second push sent while it installs an app fails with `curl failed`, usually a timeout: the board restarts into the new app before answering it. Nothing is harmed; push again once the new build is up. Set `CYD_CURL_EXTRA` to pass extra options to curl, for example `--interface`. The script never prints the key, and an agent must not run `--show-key`.

A pushed build is on trial. The board keeps it only after 60 seconds of uptime and a first drawn screen. A build that crashes or restarts before then, including a power-cycle in its first minute, has failed its start and is rolled back to the build before it. The launcher still lists that slot, but its countdown no longer defaults to it.

A launcher image has no trial. If it doesn't verify, the bootloader starts the app that `otadata` selects at power-on instead. While that app runs, a push of a good launcher or the next `launcher` release from GitHub replaces the bad one; USB works too.

New apps must link `cyd_update` (see `apps/home_dashboard` for how it is started), or call `esp_ota_mark_app_valid_cancel_rollback()` themselves. Otherwise they roll back at their next restart.

In VS Code, PlatformIO's **Upload** button runs the same script. macOS may ask once to let VS Code use the local network; allow it. The script sends everything through `curl` because macOS blocks PlatformIO's own Python from the local network.

### One-time USB step for a board with an older launcher

Boards flashed before this feature have no `cyd_update` in their app, no update key field and no bad-launcher fallback in the bootloader, so each needs one USB write. Don't erase the flash, so Wi-Fi settings and the other slot survive. Build `launcher/` and the app first, then write three images:

- `0x1000`: the launcher's `bootloader.bin`;
- `0x10000`: the launcher's `firmware.bin`;
- the app's `firmware.bin`, into the slot the board boots from now: `0x150000` for slot A (`ota_0`) or `0x2A0000` for slot B (`ota_1`).

To find the slot, read the boot log (the monitor command below): it names the running partition, `ota_0` or `ota_1`. The launcher's menu also shows which slot holds the app.

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$L = "$env:USERPROFILE\.platformio\cyd-launcher-build\esp32dev"
& $py $esptool --chip esp32 -p COM11 -b 230400 write_flash `
    0x1000 "$L\bootloader.bin" 0x10000 "$L\firmware.bin" `
    0x150000 path\to\firmware.bin
```

Leave out the partition table (`0x8000`, unchanged) and `ota_data_initial.bin`, so `otadata` keeps selecting that slot. The first `pio run -t upload` then gives the board its update key.

Until a board has had this step, its old launcher lists the `launcher` release as an app. Don't tap it there: it would put the launcher image in an app slot.

### Over USB: a new board, or recovery

The launcher needs its own bootloader, partition table and a blank `otadata`. Flashing all of them explicitly with **esptool**, at these offsets, is the verified way. Delete the project's `sdkconfig.esp32dev` before building an image for USB: the board takes the floor for a USB-flashed image from the commit time stamped in its version, and that stamp is refreshed only when CMake runs again. Don't use `pio run --target upload` in `launcher/` for this: with `upload_protocol = custom` it pushes over the network and never touches a bootloader. This erases the whole board, including the Wi-Fi settings and the update key:

```powershell
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

To put a locally built app into a slot without GitHub or the network, write its `firmware.bin` to `0x150000` (slot A) or `0x2A0000` (slot B):

```powershell
& $py $esptool --chip esp32 -p COM11 -b 230400 write_flash 0x150000 path\to\firmware.bin
```


### Updates from GitHub

With a network up, the running app checks the `launcher` release and the release for its own project when the network comes up and then every hour, at least 5 minutes apart. A release installs by itself when its notes' commit time is above 0 and above the image's floor, the commit time already installed or seen. After an install the floor is the release's commit time, so a release that then fails its good start isn't tried again. A Mac push or a USB flash also sets the floor, to its own commit time or the last release commit time the board has seen, whichever is later, so an older release never replaces a newer local build. A release published since the board's last check isn't counted yet, so a push from a checkout behind the latest release can be replaced at the next hourly check. The floor never blocks a Mac push.

## 8. Publish a new or changed application

Push to `main`. The repository's one workflow, `.github/workflows/build.yml`, builds the launcher and every folder under `apps/` on GitHub Actions, on every push and pull request. On `main` it also uploads each image as `firmware.bin` to the release tagged `launcher` or with the app's folder name, creating the release the first time. An image whose folder, `components/` and `tools/cyd_version.cmake` are unchanged since the commit in its release notes isn't republished, so the board never sees a newer commit time on identical code. The notes end with two lines the board reads: `commit: <sha>` and `commit-time: <seconds>`. Boards pick up the new build by themselves within the hour, or the launcher lists it as an update.

To add a new application, copy an existing app folder (for example `apps/home_dashboard`), set `project(...)` in its `CMakeLists.txt` to the new folder name and change `build_dir` in `platformio.ini`, write the app in `main/`, and push. The workflow picks up the new folder by itself. The project name must match the folder name, or the launcher treats a downloaded copy as flashed over USB. Include `cyd_update` as described in section 7. Boot images must stay under 1.31 MB, the slot size, and must never contain credentials, because their binaries are public.

The workflow needs the repository's Actions to have **read and write** permission for contents, so it can create releases. It asks for this with `permissions: contents: write`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `cc1.exe: fatal error: /?/C:/...` or other path errors | The project path is too long. Clone to a shorter folder. |
| esptool: `The chip stopped responding` | Retry at a lower baud rate, such as `-b 115200`. Use a short, good USB cable. |
| esptool can't open the port | Close any serial monitor, or other program, using the COM port. |
| A `sdkconfig.defaults` change has no effect | Delete `sdkconfig.esp32dev` and rebuild. |
| CI build fails on size | The workflow checks each app's `firmware.bin` against a launcher slot, 1,376,256 bytes, and the launcher's against the factory partition, 1,310,720 bytes. Make the image smaller. |
| `pio run -t upload` prints `busy <seconds>` | The board is installing, or is inside the 60 second window after a new build starts. The script waits and retries three times; run it again after that. |
| `wrong key or expired challenge` | The key on the board differs from `~/.config/cyd/update_key`, or the challenge was used up before the push arrived: something else asked the board for a challenge or pushed in between, or more than 60 seconds passed between the script's challenge request and its push. Retry; if it fails again, enter the Mac's key on the phone setup page's Updates field. |
| `no board address` | Set `CYD_HOST` or fill `~/.config/cyd/host`. |
| `No route to host` from Python | macOS blocks that Python from the local network; the script uses `curl`, so check that `curl http://cyd-xxxx.local/update/challenge` works, and that VS Code is allowed local network access. |
| A pushed or downloaded build vanished, and the old one runs | It rolled back: it didn't reach 60 seconds of uptime and its first screen. Read the serial log, fix the crash and push again. |
| The launcher is bad (power-on starts the app instead) | The bootloader found the launcher invalid. While the app runs, push a good launcher with `pio run -t upload` in `launcher/`, or let the next `launcher` release install; or flash `bootloader.bin` and the launcher's `firmware.bin` over USB, as in section 7. |
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
