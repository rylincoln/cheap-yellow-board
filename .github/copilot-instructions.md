# ESP32 CYD launcher and apps

- One repository of PlatformIO projects in native ESP-IDF C for the ESP32 Cheap Yellow Display: `launcher/` and one folder per app under `apps/`. Build each from its own folder with `pio run`.
- All projects share `components/cyd_display` through `EXTRA_COMPONENT_DIRS` in their top-level `CMakeLists.txt`. It detects the 2.8" ILI9341 or 3.5" ST7796 board from the panel ID, so screen size, font scale and text columns are runtime values (`CYD_LCD_WIDTH`, `CYD_TEXT_COLUMNS`); size file-scope arrays with the `_MAX_` constants. Pixel data goes column-major.
- The launcher is the factory app; `launcher/bootloader_components/main` makes power-on always start it. It lists releases of `IMAGE_REPO` that carry `firmware.bin` and downloads them into `ota_0`/`ota_1`.
- `.github/workflows/build.yml` builds every project and, on `main`, publishes each app on a release tagged with its folder name. An app's `project()` name must match its folder.
- No binary contains credentials. Wi-Fi, Home Assistant and wildlife camera settings live in NVS namespace `cyd_config`, set by the launcher's network chooser or its phone setup (`launcher/main/phone_setup.c`).
- Flash the launcher with esptool at the offsets in `launcher/README.md`, never `pio run --target upload`.
- `sh test/run.sh` builds and runs the host tests of the pure C modules (`launcher/main/form_check.c`, `components/ha_glance`, the wildlife parser in `components/wildlife`, and both dashboards' readings, fonts and layout) with the system compiler. `tools/make_bigfont.py` regenerates both dashboards' `bigfont_data.c`.
- Keep the launcher under 1.25 MB and apps under 1.31 MB. Verify changes with the host tests and a PlatformIO build before flashing.
