# ESP32 CYD Bootloader and Launcher

- PlatformIO project using native ESP-IDF C for an ESP32 Cheap Yellow Display. Each application has its own branch: `bootloader` (this), `wifi_checker`, `birdnet_viewer`, `screen_check`.
- The factory app is a 20-second boot menu (each tap off an entry adds 20 s) of the images in `ota_0`/`ota_1`, plus every repo branch (except `bootloader`, `main`, `How_to_Build`) whose release tagged with the branch name has `firmware.bin`, published by that branch's GitHub Actions workflow.
- No binary contains credentials. Wi-Fi and BirdNET settings live in NVS namespace `cyd_config`, set with the launcher's network chooser or its phone setup (`main/phone_setup.c`: an open access point with a captive DNS server and a web page). With no saved network, the launcher opens the chooser before its countdown.
- `bootloader_components/main` overrides the second-stage bootloader: power-on reset boots factory, any other reset boots the otadata choice.
- `components/cyd_display` (LCD, 5x7 font, patterns, touch) is copied on every application branch; keep the copies in sync. It detects the 2.8" ILI9341 or 3.5" ST7796 board from the panel ID, so screen size and font scale are runtime values (`CYD_LCD_WIDTH`, `CYD_TEXT_COLUMNS`); size static buffers with the `_MAX_` constants. The panel fills a window column-major; send pixel data in that order.
- Build with `pio run`; flash with esptool at the offsets in README.md, never `pio run --target upload`.
- Keep the launcher under 1.25 MB and boot images under 1.31 MB.
- Verify firmware changes with a PlatformIO build before flashing.
