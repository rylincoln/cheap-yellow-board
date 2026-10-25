# ESP32 CYD Wi-Fi and Bluetooth Scanner

PlatformIO/ESP-IDF application for the 320x240 or 480x320 display and resistive touchscreen on the ESP32 Cheap Yellow Display (CYD), following the [board documentation](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display).

It is one of the apps in this repository. The [root README](../../README.md) lists them all and the launcher that runs them.

## What it does

On boot the scanner scans for nearby Wi-Fi networks, then for Bluetooth Low Energy devices (6-second active scan). It then joins your Wi-Fi network and sweeps it for devices, about 30 seconds in all. Three lists share the screen in large text:

- **Wi-Fi:** signal (dBm) and SSID, strongest first.
- **Bluetooth LE:** signal (dBm) and advertised name, named devices first, then strongest first. Devices without a name show their address in gray.
- **LAN:** IP address of every device on your network that answers ARP, in address order. `GW` marks the gateway and `ME` marks this board.

The touchscreen drives the UI:

- **Entry buttons:** tap an entry to see its details. Tap anywhere to go back.
  - **Wi-Fi:** SSID, channel and RSSI, auth, cipher, BSSID, PHY/WPS. The access point the board joined also shows the board's IP, the gateway and DNS. Other networks show `IP: not joined`.
  - **BLE:** name, RSSI, address and address type, advertising type, TX power, manufacturer, first 16-bit service UUID.
  - **LAN:** IP, MAC address, and whether it is this board, the gateway or the DNS server.
- **Arrow buttons:** the up/down arrows on each list header page through that list. They are dimmed when there is nothing more in that direction.
- **Header title:** tap "WI-FI", "BLE" or "LAN" to scan again.

The full results, including Wi-Fi BSSIDs and LAN MAC addresses, are also printed to the serial log, along with the coordinates of every tap.

### Joining your network

The scanner has no credentials compiled in, so its published binary is safe. It reads `wifi_ssid` and `wifi_pass` from NVS namespace `cyd_config`, where the launcher stores them. Without them it only listens, and the LAN header reads `LAN: none`. If joining fails, the header reads `LAN: off`.

The LAN sweep covers the board's /24, 254 hosts, sending ARP requests in batches of eight. Bluetooth requires Wi-Fi modem sleep, which delays some replies, so hosts that miss the first pass get a second pass with a longer wait. A device that ignores ARP, or is asleep through both passes, won't appear.

Classic (BR/EDR) Bluetooth devices are not scanned; only BLE, using the lightweight NimBLE host.

## Build and flash

Install the PlatformIO IDE extension or PlatformIO Core. The first build installs the ESP32 platform and ESP-IDF toolchain. The project path must be short: builds fail on Windows when source paths pass 260 characters.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run
```

The app binary is `%USERPROFILE%\.platformio\cyd-build\esp32dev\firmware.bin`, about 825 KB. Use it in one of two ways:

- **As a launcher image:** every push to `main` runs the repository's `.github/workflows/build.yml`, which builds every app and publishes this one as `firmware.bin` on the release tagged `wifi_checker`. The launcher lists that release and downloads the file. You can also write it to a slot over USB, as described in the [launcher README](../../launcher/README.md).
- **On its own:** `& $pio run --target upload --upload-port COM11`. This replaces everything on the board, including the launcher.

Monitor with `& $pio device monitor --port COM11 --baud 115200`. Ctrl+] exits. If you change `sdkconfig.defaults`, delete the generated `sdkconfig.esp32dev` so the new defaults apply.

## Hardware

Both supported boards, the 2.8" ESP32-2432S028R and the 3.5" ESP32-3248S035R, are wired as listed in the [launcher README](../../launcher/README.md#hardware). `cyd_display` detects which one it is running on at start-up.
