# Weather Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A second app, `apps/weather_dashboard`, for the parents' board: outside temperature with its 24-hour high and low, rain today and this week, and the shed battery's charge now and at dawn, from Home Assistant.

**Architecture:** The home dashboard's reusable parts move into one shared component, `components/ha_glance`: the font renderer, reading parsing and formatting, tile types and drawing, the Home Assistant client, the status page and the Wi-Fi join. The home dashboard keeps its look and behaviour and only changes where that code moves. The weather dashboard is a small app on top: its own layout, fonts and settings keys. The launcher's phone page gains a second group of seven sensor fields.

**Tech Stack:** ESP-IDF 6.1 via PlatformIO `espressif32@7.1.3`, plain C11 host tests (`test/run.sh`), Python font generator (`tools/make_bigfont.py`, Pillow 13.0.0 and matplotlib 3.11.2 pinned).

**Spec:** `docs/superpowers/specs/2026-10-26-weather-dashboard-design.md`

## Global Constraints

- Build: `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run` in the project folder. Host tests: `sh test/run.sh`. After changing which source files a project or component compiles, delete that project's `sdkconfig.esp32dev` so CMake re-runs.
- Fonts: `uv run --no-project --with pillow==13.0.0 --with matplotlib==3.11.2 python tools/make_bigfont.py` regenerates every app's `bigfont_data.c`/`.h`; never edit those by hand.
- No entity ID, token, address or Wi-Fi detail in any committed file; examples use `sensor.some_name`. `git grep -nE "eyJ|sensor\.[a-z0-9]+_[a-z0-9_]+" -- . ':!docs/superpowers/plans/2026-10-25-home-dashboard.md' | grep -v "sensor\.some_name"` prints nothing.
- Commits in the user's voice (short imperative subject, as in `git log`), author Ry Blaisdell <rylincoln@gmail.com>, no AI attribution of any kind.
- Never run `tools/cyd_push.py --show-key`, never print a key or token, never use `pio run -t upload` for USB (it pushes over Wi-Fi).
- An app's CMake `project()` name equals its folder name: `weather_dashboard`.
- No PSRAM; tiles draw in strips of 16 rows; no full frame buffer.
- Colors (RGB): tile `#141414`, label gray `#9A9A9A`, stale/dry gray `#707070`, blue `#7FD4FF`, amber `#FFD38A`, light gray `#E6E6E6`, red `#FF8A7A`.
- Code style: match the surrounding file (comment density, naming, brace style). Comments are short and say why.

## Review Focus

1. **A dawn forecast below zero** (the projection says the battery runs out) must read `0` in red with an empty battery, not `--`. Test in Task 4.
2. **The widest readings on both panels** (`100`/`100` in the shed strip, `-10°`/`100°` and `--` in the outside tile, `99.9` rain) must stay inside their tiles. Test in Task 4.
3. **Seven entity IDs of the maximum 95 characters** must fit the weather app's request buffer. Test in Task 4.
4. **The high and low sensors not yet made in Home Assistant**: the other readings show normally and the high and low show `--`. Test in Task 4.
5. **The home dashboard after the refactor** must compose byte-identical tiles for the same readings. Its existing layout tests stay green unchanged except for include paths and the counted readings calls (Tasks 1 and 2).

---

### Task 1: Shared component for fonts, readings, tiles and the HA client

**Files:**
- Create: `components/ha_glance/CMakeLists.txt`, `components/ha_glance/glance_tile.c`, `components/ha_glance/include/glance_tile.h`, `apps/home_dashboard/main/sensors.h`
- Move (`git mv`): `apps/home_dashboard/main/bigfont.{c,h}`, `readings.{c,h}`, `ha_client.{c,h}` → `components/ha_glance/*.c` and `components/ha_glance/include/*.h`; `apps/home_dashboard/main/glance.{c,h}` → `components/ha_glance/glance_draw.c` and `include/glance_draw.h`
- Modify: `apps/home_dashboard/main/{CMakeLists.txt,main.c,glance_layout.c,glance_layout.h}`, `test/run.sh`, `test/test_readings.c`, `test/test_glance_layout.c`
- Test: `test/test_glance_tile.c` (new)

**Interfaces:**
- Produces, in `readings.h` (no sensor enum any more):
  - `int readings_template_body(const char *const entity_ids[], int count, char *out, size_t size);`
  - `int readings_apply(const char *reply, const char *const entity_ids[], int count, int64_t now_us, reading_t readings[]);` A reading's `available` flag doubles as the "already matched" mark, so no fixed-size array is needed.
  - Unchanged: `reading_t`, `READING_STALE_US`, `reading_stale`, `format_temperature`, `format_battery`, `format_percent`, `battery_direction_t`.
- Produces, in `glance_tile.h`: `rect_t`, `glance_text_t` (unchanged fields), `#define GLANCE_MAX_TEXTS 8`, `glance_tile_t` with `glance_text_t texts[GLANCE_MAX_TEXTS]`, and:
  - `uint16_t glance_rgb(uint8_t r, uint8_t g, uint8_t b);`
  - `rect_t glance_scale_rect(rect_t design, int width, int height);` from the 480x320 design grid, rounding as `scale()` in `glance_layout.c` does today.
  - `void glance_add_text(glance_tile_t *tile, const bigfont_t *font, const char *text, int x, int line_y, uint16_t color);` A ninth text is dropped.
- Produces, in `glance_draw.h`: `#define GLANCE_MAX_TILES 8` and `void glance_draw(const glance_tile_t *tiles, int count, bool full);`
- Produces, in `apps/home_dashboard/main/sensors.h`: the existing `sensor_t` enum, moved out of `readings.h` unchanged.

- [ ] **Step 1: Write the failing tests.** In `test/test_glance_tile.c`: `glance_scale_rect((rect_t){4, 244, 472, 72}, 320, 240)` equals `{3, 183, 315, 54}`; nine `glance_add_text` calls leave `count == 8`; `glance_rgb(0xFF, 0x8A, 0x7A) == 0xFC4F`. In `test/test_readings.c`, change every `readings_template_body`/`readings_apply` call to pass the count (`5` for the existing ID lists); expected strings and values stay as they are.
- [ ] **Step 2: Run `sh test/run.sh`; expect a compile failure** (missing `glance_tile.h`, wrong argument counts).
- [ ] **Step 3: Move and generalize.** `git mv` the files listed above; `components/ha_glance/CMakeLists.txt` registers `bigfont.c readings.c glance_tile.c glance_draw.c ha_client.c` with `INCLUDE_DIRS "include"` and the `PRIV_REQUIRES` the moved code needs (`cyd_display esp_http_client esp_timer log mbedtls` or as the compiler asks). Home `main/CMakeLists.txt` drops the moved sources and requires `ha_glance`. `glance_layout.c` uses `glance_scale_rect`/`glance_add_text`; its own `scale` and `add_text` go. `glance_draw` keeps its "skip a tile identical to the one last drawn" behaviour for up to `GLANCE_MAX_TILES`. In `test/run.sh`, add `-Icomponents/ha_glance/include` and the moved source paths, and a build-and-run pair for `test_glance_tile.c`.
- [ ] **Step 4: Run `sh test/run.sh`; expect `host tests passed`.** `test_glance_layout.c`'s assertions are unchanged.
- [ ] **Step 5: Build.** Delete `apps/home_dashboard/sdkconfig.esp32dev`, then build the home dashboard; expect `SUCCESS` and no new warnings.
- [ ] **Step 6: Commit** (`git add -A components/ha_glance apps/home_dashboard test`): "Move the dashboard's shared parts into a component".

### Task 2: Shared status page and Wi-Fi join

**Files:**
- Create: `components/ha_glance/board_status.c`, `include/board_status.h`, `components/ha_glance/board_wifi.c`, `include/board_wifi.h`
- Modify: `components/ha_glance/CMakeLists.txt`, `apps/home_dashboard/main/main.c`

**Interfaces:**
- Consumes: Task 1's component.
- Produces, in `board_status.h` (the crash record, start reason and status fields move here from home `main.c`, `RTC_NOINIT_ATTR` included):
  - `void board_status_note_start(void);` at start, as `note_start` today.
  - `void board_status_set_stage(const char *stage);`
  - `void board_status_set_wifi(const char *format, ...)` and `void board_status_set_ha(const char *format, ...)`, both `__attribute__((format(printf, 1, 2)))`.
  - `bool board_status_changed(void);` True when a status changed since the last draw.
  - `void board_status_draw(const char *title, const char *ssid, const char *ha_url, const char *hint);` The same ten rows as today's `draw_status`, with `title` on row 0.
- Produces, in `board_wifi.h` (today's `wifi_init`, event handler and `wifi_failure_text`):
  - `void board_wifi_start(const char *ssid, const char *password, bool ha_ready, TaskHandle_t task, uint32_t joined_bit);` Joins and retries for good, reports with `board_status_set_wifi`, and on each address calls `cyd_update_network_up()` and notifies `task` with `joined_bit`. When `ha_ready`, a lost connection sets the HA status to `waiting for Wi-Fi`.
  - `bool board_wifi_connected(void);`
- Stays in home `main.c`: the screen states, birds, `server_status` (with its own lock), config loading, `fetch_readings`, the task loop and `app_main`.

- [ ] **Step 1: Move the code.** Home `main.c` calls the new functions. The status page title stays `HOME DASHBOARD`, and the hint logic stays in `main.c`.
- [ ] **Step 2: Run `sh test/run.sh` (expect `host tests passed`), then build the home dashboard after deleting its `sdkconfig.esp32dev`.** Expect `SUCCESS`, no new warnings, and the dashboard's flash use within 2 KB of before.
- [ ] **Step 3: Commit:** "Share the status page and Wi-Fi join".

(The controller pushes this build to the user's board in Task 7, before the weather work reaches hardware.)

### Task 3: Font generator writes both apps' fonts

**Files:**
- Modify: `tools/make_bigfont.py`
- Create (generated): `apps/weather_dashboard/main/bigfont_data.c`, `apps/weather_dashboard/main/bigfont_data.h`

**Interfaces:**
- Produces, in the weather app's `bigfont_data.h`, `bigfont_<name>_480` and `bigfont_<name>_320` for each of:

  | name | em (480, 320) | characters |
  |---|---|---|
  | `label` | (20, 14) | the home `LABEL_CHARS` (letters, digits, `-`, `%`, space) |
  | `small` | (14, 10) | space, `A`–`Z`, `0`–`9` |
  | `hilo` | (32, 21) | space, digits, `-`, `°`, `▲`, `▼` |
  | `rain` | (48, 28) | space, digits, `.`, `-` |
  | `shed` | (40, 24) | space, digits, `-`, `→`, sunrise `U+E010`, battery `U+E000`–`U+E00A` |
  | `temp` | (120, 80) | space, digits, `-`, `°` |
  | `temp_mid` | fitted | as `temp`; the largest em below `temp`'s at which `-10°` and `100°` fit 272 px (480) and 176 px (320) |

- `▲`, `▼` and the battery levels are drawn as the home fonts draw them. `→` comes from DejaVu Sans Bold. The sunrise icon is drawn at `U+E010`: a half sun on a horizon line with short rays, as tall as a digit, standing on the baseline.

- [ ] **Step 1: Restructure the generator** around one table per app (output folder plus a list of fonts, each with its sizes, characters and an optional fit rule), keeping the shared drawing helpers. Run the pinned command; `main()` writes both apps.
- [ ] **Step 2: Verify the home fonts are unchanged:** `git diff --exit-code apps/home_dashboard/main/bigfont_data.c apps/home_dashboard/main/bigfont_data.h` prints nothing.
- [ ] **Step 3: Verify the weather fonts:** the generator's printout lists 14 weather fonts, `temp_mid` has an em above `hilo`'s, and `cc -std=c11 -Wall -Wextra -Werror -c -Icomponents/ha_glance/include apps/weather_dashboard/main/bigfont_data.c -o /dev/null` succeeds.
- [ ] **Step 4: Commit:** "Generate the weather dashboard's fonts".

### Task 4: Weather layout and rain format

**Files:**
- Create: `apps/weather_dashboard/main/weather_layout.c`, `apps/weather_dashboard/main/weather_layout.h`, `test/test_weather_layout.c`
- Modify: `components/ha_glance/readings.c`, `components/ha_glance/include/readings.h`, `test/test_readings.c`, `test/run.sh`; `tools/make_bigfont.py` only to lower the `shed` em if Step 2 shows it doesn't fit

**Interfaces:**
- Consumes: Task 1's `readings.h`, `glance_tile.h`; Task 3's fonts.
- Produces, in `readings.h`: `int format_rain(const reading_t *reading, char *out, size_t size);` returns 1 for rain, 0 for a dry `0.00`, and -1 for `--`.
- Produces, in `weather_layout.h`:
  - `typedef enum { WX_TEMP, WX_HIGH, WX_LOW, WX_RAIN_TODAY, WX_RAIN_WEEK, WX_SHED_PCT, WX_SHED_DAWN, WX_COUNT } wx_sensor_t;`
  - `typedef enum { TILE_OUTSIDE, TILE_RAIN_TODAY, TILE_RAIN_WEEK, TILE_SHED, WEATHER_TILE_COUNT } weather_tile_t;`
  - `#define WX_BODY_SIZE 1024` (the request buffer `main.c` uses)
  - `void weather_compose(int width, int height, const reading_t readings[WX_COUNT], int64_t now_us, glance_tile_t tiles[WEATHER_TILE_COUNT]);` Zeroes the tiles first.

- [ ] **Step 1: Write the failing tests.**
  - `format_rain` in `test/test_readings.c`:

    | input | text | result |
    |---|---|---|
    | 0 | `0.00` | 0 |
    | 0.004 | `0.00` | 0 |
    | 0.12 | `0.12` | 1 |
    | 1.48 | `1.48` | 1 |
    | 9.994 | `9.99` | 1 |
    | 9.996 | `10.0` | 1 |
    | 12.34 | `12.3` | 1 |
    | 99.94 | `99.9` | 1 |
    | 99.96 | `--` | -1 |
    | -0.01 | `--` | -1 |
    | NaN | `--` | -1 |
    | unavailable | `--` | -1 |

  - `test/test_weather_layout.c`, at 480x320 and 320x240 unless stated:
    - **Rects.** At 480x320 the tiles are `{4,4,288,236}`, `{296,4,180,116}`, `{296,124,180,116}`, `{4,244,472,72}`, background `#141414`.
    - **A normal reading** (temp 47, high 58, low 39, rain 0.12 and 1.48, shed 82.4, dawn 64.2, all fresh):
      - outside: `OUTSIDE` label gray, `47°` blue, `▲58°` amber, `▼39°` blue, `PAST 24 HOURS`;
      - rain: `RAIN TODAY` / `0.12` light gray / `in`, and `THIS WEEK` / `1.48` / `in`;
      - shed: `SHED`, `"\xEE\x80\x88" "82"` light gray, `%`, `→`, `"\xEE\x80\x90" "64"` amber, `%`, `AT DAWN`.
    - **Fit.** Every text lies inside its tile (the `inside()` helper from `test_glance_layout.c`) for:
      - temp `-10`, `100`, `-100` and unavailable;
      - high and low `100` and `-10`;
      - rain `99.9`;
      - shed and dawn `100` and unavailable.
    - **Centring.** The shed strip's texts are centred within 1 px.
    - **Number size.** The temperature uses `temp` for `47`, `temp_mid` for `-10` and `100`, and `hilo` for `-100`.
    - **Old data.** Temp and shed older than `READING_STALE_US` are gray. High, low, rain and dawn that old keep their colors.
    - **Rain colors.** Rain `0` is `0.00` in gray. An unavailable rain is `--` in gray.
    - **Low charge.** Shed `20` is red with battery level `\xEE\x80\x82`, and shed `21` is light gray. Dawn `20` is red.
    - **Missing charge.** Shed unavailable shows `"\xEE\x80\x80" "--"` (an empty battery) in gray, and dawn unavailable shows `"\xEE\x80\x90" "--"` in gray.
    - **Dawn below zero (Review Focus 1).** Dawn `-12` shows `"\xEE\x80\x90" "0"` in red.
    - **Missing high and low (Review Focus 4).** High and low unavailable show `▲--` and `▼--` in gray; the temperature still shows normally.
    - **Long IDs (Review Focus 3).** `readings_template_body` with seven 95-character IDs into `char body[WX_BODY_SIZE]` returns > 0.
    - **Same input, same tiles.** Two composes into differently pre-filled arrays compare equal with `memcmp`.
- [ ] **Step 2: Run `sh test/run.sh`; expect it to fail** (no `weather_layout.h` or `format_rain`).
- [ ] **Step 3: Implement `format_rain` and `weather_compose`.**
  - Use the rects above, scaled with `glance_scale_rect`.
  - **Outside tile.** The label is at the top, as the home temperature tiles do. The temperature is centred in the space left above the high/low row, stepping down `temp`, `temp_mid`, `hilo` until it is 16 px narrower than the tile. The high/low pair is centred with a gap, and `PAST 24 HOURS` (`small` font) goes under it.
  - **Rain tiles.** The label is at the top, with the value and a small gray `in` (label font) centred together below it.
  - **Shed strip.** One centred line:
    - `SHED` (`small`);
    - battery glyph plus charge (`shed` font), then `%` (`label`);
    - `→` (`shed`, gray);
    - sunrise plus dawn (`shed`), then `%` (`label`);
    - `AT DAWN` (`small`).
  - **Battery fill.** Tenths are `(percent + 5) / 10`, an empty battery when `--`.
  - **Dawn below zero.** A dawn value below -0.5 is treated as 0.
  - If the widest shed case doesn't fit, lower the `shed` em in the generator (not below 32 at 480 or 20 at 320) and regenerate.
- [ ] **Step 4: Run `sh test/run.sh`; expect `host tests passed`.**
- [ ] **Step 5: Commit:** "Lay out the weather dashboard".

### Task 5: The weather dashboard app

**Files:**
- Create in `apps/weather_dashboard/`:
  - copied from `apps/home_dashboard`: `.vscode/extensions.json`, `.vscode/tasks.json`, `partitions.csv`, `sdkconfig.defaults`;
  - `CMakeLists.txt`, the home one with `project(weather_dashboard)`;
  - `platformio.ini`, the home one with `build_dir = ${sysenv.USERPROFILE}/.platformio/cyd-weather-build`;
  - `main/CMakeLists.txt`, `main/main.c`, `README.md`.
- Commit the generated `dependencies.lock`.

**Interfaces:**
- Consumes: Tasks 1, 2 and 4.
- Settings keys, in `wx_sensor_t` order: `ha_wx_temp`, `ha_wx_high`, `ha_wx_low`, `ha_rain_today`, `ha_rain_week`, `ha_shed_pct`, `ha_shed_dawn`. All seven are required: `no sensor IDs` until they're set; `sensor IDs too long` if the body overflows `WX_BODY_SIZE`.

- [ ] **Step 1: Write `main.c`.** It mirrors the home dashboard minus birds, photos and timed screens.
  - **Start-up.** `cyd_display_init`, then `board_status_note_start`. Load the settings from `cyd_config`: Wi-Fi, `ha_url` (trailing slashes stripped), `ha_token` and the seven IDs. Then build the request body.
  - **Status page.** Titled `WEATHER DASHBOARD`, with the home hint logic, until the first reading arrives.
  - **Refresh.** Fetch every 30 s while the status page or readings are up. Recompose every second and draw only changed tiles.
  - **Taps.** A tap on the status page retries now; a tap on the readings does nothing.
  - **Updates.** `cyd_update_init` with label `weather`, the screen bit, `cyd_update_screen_drawn` and `cyd_update_set_interruptible(true)` on the readings. `cyd_update_lend_screen` handling is the same as the home dashboard's.
  - **No Wi-Fi settings.** Shows the home dashboard's `NO WI-FI SETTINGS` text.
  - Log tag `weather_dashboard`.
- [ ] **Step 2: Build** (`cd apps/weather_dashboard && USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run`). Expect `SUCCESS`, no warnings from `main/`, and flash use under 1,376,256 bytes.
- [ ] **Step 3: Write `README.md`** in the home README's shape, covering:
  - what it shows;
  - the colors and gray rules;
  - the settings table, keys only, with no IDs;
  - that the high and low come from two Statistics helpers in Home Assistant (max and min of the temperature over 24 hours);
  - build and push.
- [ ] **Step 4: Commit:** "Add the weather dashboard app".

### Task 6: Phone page fields and docs

**Files:**
- Modify: `launcher/main/phone_setup.c`, `launcher/main/phone_setup.h`, `launcher/main/main.c`, `launcher/README.md`, `CLAUDE.md`, `docs/INSTALL.md`, `README.md`

**Interfaces:**
- Produces, in `phone_setup.h`: `#define PHONE_HA_SENSOR_COUNT 12` and `extern const char *const PHONE_HA_SENSOR_KEYS[PHONE_HA_SENSOR_COUNT];`, defined once in `phone_setup.c`. The keys are the home five (`ha_upstairs` … `ha_battery_pct`) followed by Task 5's seven. All three `SENSOR_KEYS` copies in `launcher/main/main.c`, and the one in `phone_setup.c`'s submit handler, use it.

- [ ] **Step 1: Change the page.**
  - **Home dashboard group.** Before the first sensor field the page shows `<h3>Home dashboard</h3>`, with the existing five labels.
  - **Weather dashboard group.** Before the sixth it shows `<h3>Weather dashboard</h3>`, labelled `Outside temperature`, `High, past 24 hours`, `Low, past 24 hours`, `Rain today`, `Rain this week`, `Shed battery %`, `Shed battery % at dawn`.
  - **Form buffer.** `FORM_MAX` becomes 5632 (every field percent-encoded).
- [ ] **Step 2: Build the launcher** (delete nothing; no source list changes). Expect `SUCCESS` and no new warnings. Then run `sh test/run.sh` and expect `host tests passed`.
- [ ] **Step 3: Update docs.**
  - **`CLAUDE.md`.** In the Settings list, add the seven keys after `ha_battery_pct`. In Layout, name `components/ha_glance` as the dashboards' shared code.
  - **`launcher/README.md`.** Add a row for the seven keys.
  - **`docs/INSTALL.md`.** In section 5, add a "The weather dashboard needs" list. In section 8 or 7, cover setting up a second board: flash the launcher over USB, join Wi-Fi, fill in the settings from a laptop at the board's address while phone setup runs, then install or push the app.
  - **Root `README.md`.** Mention the second app.
- [ ] **Step 4: Check the secrets rule** from Global Constraints: the command prints nothing.
- [ ] **Step 5: Commit:** "Add the weather dashboard's settings to the phone page".

### Task 7: On hardware (controller and user, not a subagent)

- [ ] **Step 1: The user's board.**
  - Push the refactored home dashboard with `pio run -t upload` in `apps/home_dashboard`.
  - Wait 90 s for its trial, then push the launcher.
  - The user confirms the dashboard looks as before.
- [ ] **Step 2: Home Assistant.** The user creates two Statistics helpers, the maximum and the minimum of the station temperature over 24 hours. Creating them for the user needs their explicit yes.
- [ ] **Step 3: The second board.**
  - With the user's go-ahead, it goes on the Mac's USB; run serial commands with the sandbox disabled.
  - `erase_flash`, then write the launcher with the esptool command in `CLAUDE.md`.
  - Join Wi-Fi from the launcher's network chooser or phone setup.
  - Push the weather dashboard from `apps/weather_dashboard`. The first push gives a keyless board the Mac's key.
- [ ] **Step 4: Settings.** While phone setup runs, the user opens the board's LAN address on the laptop and pastes:
  - the HA address and token;
  - the seven IDs, which go only into the page, never into a file.
- [ ] **Step 5: Verify.**
  - The weather screen shows all seven readings.
  - Readings that are old by design stay colored.
  - The board keeps the build after 60 s.
