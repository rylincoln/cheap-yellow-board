# Home Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every app with `apps/home_dashboard`. It shows three temperatures and the Shop battery current from Home Assistant, with the latest BirdNET bird one tap away. Its Home Assistant settings are entered on the launcher's phone page.

**Architecture:**
- The dashboard is the bird viewer moved and extended. Its existing ESP-IDF code (Wi-Fi, status page, bird fetch, JPEG streaming, bird details) stays in `main.c`.
- New logic goes in small pure-C modules that the host compiler can test:
  - `readings.c`: parse Home Assistant's reply and format the numbers.
  - `bigfont.c`: an anti-aliased bitmap font renderer.
  - `glance_layout.c`: where each tile and string goes.
- Two thin ESP-only files use those modules:
  - `ha_client.c`: one HTTP POST to Home Assistant.
  - `glance.c`: draws tiles in strips with `cyd_draw_bitmap`.
- The launcher gains a pure `form_check.c` plus six fields on its phone page.

**Tech Stack:** C11, ESP-IDF 6.1 through PlatformIO `espressif32@7.1.3`, the shared `components/cyd_display`. Host tests use the system `cc` and no framework. The font generator runs under Python with Pillow and matplotlib, via `uv run`.

**Spec:** `docs/superpowers/specs/2026-10-25-home-dashboard-design.md`

## Global Constraints

- **Build check:** run `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run` in the project folder; it must exit 0 with no new warnings. Host test check: `sh test/run.sh` exits 0.
- **Flash budget:** `home_dashboard`'s `firmware.bin` must be at most 1,376,256 bytes, the launcher slot size that CI checks.
- **Nothing household-specific in the repo:**
  - No token, password or household entity ID in any committed file or binary.
  - Example IDs in tests and docs are `sensor.a`…`sensor.d` or `sensor.some_name`.
- **Repository conventions:**
  - No LVGL and no cJSON. JSON and text are scanned by hand, as the viewer does.
  - Static buffers are sized with `CYD_LCD_MAX_WIDTH`, `CYD_TEXT_MAX_COLUMNS` and similar. Nothing display-sized uses the 480 or 320 literal.
  - No frame buffer. Draw in strips of at most `CYD_LCD_MAX_WIDTH * 16` pixels.
- **Host-only modules:** the pure modules (`form_check.c`, `readings.c`, `bigfont.c`, `bigfont_data.c`, `glance_layout.c`, and from Task 8 `json_scan.c` and `wildlife.c`) include only the C standard library, so `test/run.sh` can build them with the host `cc`.
- **Project names:** `project()` equals the folder name.
- **Settings keys:** `cyd_config` keys are `ha_url`, `ha_token`, `ha_upstairs`, `ha_downstairs`, `ha_outside` and `ha_battery`, and (from Task 8) `wildlife_url`, which replaces `birdnet_url`.
- **Field limits:**

  | Field | Maximum characters |
  |---|---|
  | `ha_url` | 127 |
  | `ha_token` | 255 |
  | each entity ID | 95 |

- **Commits** carry no AI attribution. Never push, and never delete a GitHub release, without the user's go-ahead in chat.
- **Code style:** follow the surrounding code. Four-space indents, `/* */` comments above functions saying what they do, `static` for anything file-local, and `ESP_LOGx` for diagnostics.

## Review Focus

1. **Winter and summer temperatures:** `-12°` outside and `100°` in any tile must fit inside their tile on both panels, with no clipping and no overlap with the label. Pinned in Task 6.
2. **Messy Home Assistant replies:** a reply with CRLF line endings, no trailing newline, an `unavailable` state, an entity missing, or a garbage line must still yield correct readings for the lines that are good. Pinned in Task 4.
3. **Battery values at the rounding edges:** 99.96 A shows `100`, 99.94 A shows `99.9`, 0.05 A shows idle, 999.4 A shows `999`, and 1075.1 A shows `--`. Pinned in Task 4.
4. **Saving the phone page without retyping the token:**
   - A blank token with an unchanged Home Assistant address keeps the saved token.
   - A blank token with a changed address erases it.
   - Pinned in Task 1.
5. **A dead sensor on a live Home Assistant:** a reading fetched just now, but last reported 301 s ago, is stale. One last reported 299 s ago is not. Pinned in Task 4.

---

### Task 1: Host test harness and the launcher's form checks

**Files:**
- Create: `test/check.h`, `test/run.sh`, `test/test_form_check.c`
- Create: `launcher/main/form_check.h`, `launcher/main/form_check.c`
- Modify: `launcher/main/phone_setup.c`: delete its static `password_valid` and `url_valid`, include `form_check.h`, and call the moved versions.
- Modify: `launcher/main/CMakeLists.txt`: add `"form_check.c"` to `SRCS`.
- Modify: `.github/workflows/build.yml`: add a `host-tests` job.

**Interfaces:**
- Produces, in `form_check.h`:
  - `bool form_password_valid(const char *password);` is the old `password_valid`, unchanged.
  - `bool form_url_valid(char *url, size_t size);` is the old `url_valid`, unchanged: it adds `http://` to a bare host.
  - `bool form_entity_id_valid(const char *entity_id);` is true for `""`, or for `domain.object_id`: both parts non-empty, each `[a-z0-9_]+`, exactly one dot.
  - `bool form_token_kept(const char *token, const char *ha_url, const char *saved_ha_url);` is true when `token` is empty and `ha_url` equals `saved_ha_url`.
- Produces `test/check.h` for every later host test:
  - `CHECK(cond)` and `CHECK_STR(actual, expected)` print `file:line` and count a failure.
  - `CHECK_DONE()` returns from `main` with a nonzero status if anything failed.

- [ ] **Step 1: Write `test/check.h`, `test/run.sh` and the failing test**

`test/run.sh` is POSIX `sh`. It starts with `set -e` and `cd "$(dirname "$0")/.."`, and builds into `build/host-tests/`, which is git-ignored. Each test is one line:

```sh
cc -std=c11 -Wall -Wextra -Werror -Ilauncher/main test/test_form_check.c launcher/main/form_check.c -o build/host-tests/form_check && build/host-tests/form_check
```

The script prints `host tests passed` at the end.

`test/test_form_check.c` checks:

```c
CHECK(form_entity_id_valid(""));
CHECK(form_entity_id_valid("sensor.some_name_1"));
CHECK(!form_entity_id_valid("sensor"));
CHECK(!form_entity_id_valid("Sensor.a"));
CHECK(!form_entity_id_valid("sensor.a.b"));
CHECK(!form_entity_id_valid(".a"));
CHECK(!form_entity_id_valid("sensor."));
CHECK(!form_entity_id_valid("sensor.a b"));
CHECK(!form_entity_id_valid("sensor.a'"));
CHECK(form_token_kept("", "http://h:8123", "http://h:8123"));
CHECK(!form_token_kept("", "http://other:8123", "http://h:8123"));
CHECK(!form_token_kept("abc", "http://h:8123", "http://h:8123"));
CHECK(form_password_valid("") && form_password_valid("12345678") && !form_password_valid("short"));
char url[32] = "192.168.1.5:8123";
CHECK(form_url_valid(url, sizeof(url)));
CHECK_STR(url, "http://192.168.1.5:8123");
```

- [ ] **Step 2: Run `sh test/run.sh`.** Expected: it fails to compile, because `form_check.h` doesn't exist yet.

- [ ] **Step 3: Implement `launcher/main/form_check.c`.** Move the two functions, rename them, and add the two new ones. Include only `<ctype.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>` and `<strings.h>`.

- [ ] **Step 4: Run `sh test/run.sh`.** Expected: `host tests passed`.

- [ ] **Step 5: Add the CI job.** In `.github/workflows/build.yml`, add a `host-tests` job on `ubuntu-latest` with `actions/checkout@v4` and `run: sh test/run.sh`. Build the launcher and confirm it still compiles with the moved functions.

- [ ] **Step 6: Commit.** `git add test launcher/main .github/workflows/build.yml`, then commit with the message "Check phone setup fields in host tests".

### Task 2: The Home Assistant section on the phone page

**Files:**
- Modify: `launcher/main/phone_setup.h` (whole public API)
- Modify: `launcher/main/phone_setup.c`: `FORM_MAX`, `form_field`, `send_form`, `page_get`, `save_post`, `copy_config`
- Modify: `launcher/main/main.c:278-300` (`save_phone_settings`) and `:1852-1866` (`phone_setup_screen`)
- Modify: `launcher/README.md` (settings table), `CLAUDE.md` (Settings bullet)

**Interfaces:**
- Consumes: Task 1's `form_entity_id_valid`, `form_url_valid` and `form_token_kept`.
- Produces, in `phone_setup.h`, replacing the old save callback and config:

```c
#define PHONE_HA_SENSOR_COUNT 4 /* upstairs, downstairs, outside, battery */

/* What the page submits, and what it shows when drawn again. */
typedef struct {
    char ssid[33];
    char password[65];
    char birdnet_url[128];
    char ha_url[128];
    char ha_token[256];
    char ha_sensors[PHONE_HA_SENSOR_COUNT][96];
} phone_settings_t;

typedef bool (*phone_setup_save_fn)(const phone_settings_t *settings);

typedef struct {
    const wifi_ap_record_t *networks;
    int network_count;
    const phone_settings_t *saved; /* shown on the page; password and token ignored */
    bool ha_token_saved;           /* says "blank keeps the saved token" */
    phone_setup_save_fn save;
} phone_setup_config_t;
```

- [ ] **Step 1: Change the header and `phone_setup.c`.**
  - **Form buffer:** `FORM_MAX` becomes 3072. `save_post`'s `form` buffer becomes `static`; the server runs one handler at a time.
  - **`form_field`'s scratch buffer:** size it `3 * 255 + 1`, the longest field percent-encoded, instead of `FORM_MAX`.
  - **Page layout:** below the BirdNET field, add a `Home Assistant` heading and six inputs, all with `autocapitalize="none" autocorrect="off" spellcheck="false"`:
    - `ha_url` (maxlength 127, `inputmode="url"`);
    - `ha_token` (`type="password"`, maxlength 255, never prefilled);
    - `ha_upstairs`, `ha_downstairs`, `ha_outside` and `ha_battery` (maxlength 95, labelled Upstairs, Downstairs, Outside and Shop battery, prefilled from `saved`).
  - **Token hint:** when `ha_token_saved` is set, a `<small>` under the token says "Blank keeps the saved token."
  - **BirdNET label:** the field's `<small>` changes from `for the BirdNET viewer` to `for birds on the dashboard`.
  - **Error messages** in `save_post`, checked in this order after the existing ones:
    - "The Home Assistant address must be an http:// address."
    - "The token is too long."
    - "Sensor IDs look like sensor.some_name."
  - **On error**, the form is drawn again from the submitted values, never with the password or token.

- [ ] **Step 2: Change `save_phone_settings(const phone_settings_t *settings)` in `launcher/main/main.c`.**
  - Wi-Fi and `birdnet_url` are handled as today.
  - Then, in the same `nvs_open`/`nvs_commit`:
    - `ha_url`: set it, or erase it when blank.
    - `ha_token`: keep the saved one when `form_token_kept(...)`; otherwise set it, or erase it when blank.
    - Each sensor key: set it, or erase it when blank.
  - `ESP_ERR_NVS_NOT_FOUND` from an erase counts as success, as it does today.

  `phone_setup_screen` loads all of these with `load_config_string` into a `static phone_settings_t`. It sets `ha_token_saved` from whether `ha_token` is non-empty.

- [ ] **Step 3: Build the launcher.** Run `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run` in `launcher/`. Expected: exit 0 with no warnings. Also run `sh test/run.sh`, which should still pass.

- [ ] **Step 4: Try it on the board.**
  - Flash only the launcher's `firmware.bin` to `0x10000` with esptool, unsandboxed, without `erase_flash`, so the saved Wi-Fi survives.
  - Open PHONE SETUP and have the user join `CYD-Setup-XXXX`.
  - The user enters their Home Assistant address, a long-lived token and the four sensor IDs, then saves.
  - **Expected:** the board shows "SAVED BY PHONE" and rejoins Wi-Fi.
  - Opening PHONE SETUP again shows the address and the four IDs prefilled, the token blank with the "Blank keeps the saved token" hint, and the BirdNET field unchanged.

- [ ] **Step 5: Update the docs.**
  - Add the six keys to `launcher/README.md`'s settings table, and to the Settings bullet in `CLAUDE.md`.
  - Add the README note: to update the launcher without losing settings, write only `firmware.bin` to `0x10000`.

- [ ] **Step 6: Commit.** "Take Home Assistant settings on the phone page"

### Task 3: Remove the other eight apps

**Files:**
- Delete: `apps/CatMouse`, `apps/Datalogger`, `apps/bluetooth_diag`, `apps/gear_roll`, `apps/screen_check`, `apps/stopwatch`, `apps/touchscreen_cal`, `apps/wifi_checker`
- Modify:
  - `README.md`: the app table.
  - `docs/INSTALL.md`: the layout table, the "copy `apps/screen_check` or `apps/stopwatch`" advice, the troubleshooting row that says to run `touchscreen_cal`, and LVGL in the "At a glance" table.
  - `launcher/README.md:31`: drop "or the `wifi_checker` CONNECT screen".
  - `.github/workflows/build.yml:32`: the comment's example becomes `["launcher", "home_dashboard"]`.
  - `components/cyd_display/include/cyd_display.h:70`: the calibration is saved in NVS as `touch_cal`. No app writes it now; a saved one is still applied.
  - `CLAUDE.md` and `.github/copilot-instructions.md`, wherever they list apps.

- [ ] **Step 1: Remove the folders.** Run `git rm -r -q` on the eight folders, then `rm -rf` the same paths. That also removes the git-ignored `sdkconfig.esp32dev` and `managed_components/` left behind. Check first that `git status --short apps/` lists only those deletions.

- [ ] **Step 2: Edit the files listed above.** Then run `grep -rnE "CatMouse|Datalogger|bluetooth_diag|gear_roll|screen_check|stopwatch|touchscreen_cal|wifi_checker" --exclude-dir=.git --exclude-dir=superpowers .`. Expected: no output.

- [ ] **Step 3: Build the launcher and `apps/birdnet_viewer`.** Both should exit 0.

- [ ] **Step 4: Commit.** "Keep only the bird viewer"

### Task 4: Move the viewer to home_dashboard; parse, format and age readings (pure)

**Files:**
- Move: `git mv apps/birdnet_viewer apps/home_dashboard`, then `rm -rf apps/birdnet_viewer` for the ignored leftovers.
- Modify:
  - `apps/home_dashboard/CMakeLists.txt`: `project(home_dashboard)`.
  - `platformio.ini`: `build_dir = ${sysenv.USERPROFILE}/.platformio/cyd-dashboard-build`.
- Create:
  - `apps/home_dashboard/partitions.csv`: `nvs 0x9000 0x6000`, `phy_init 0xf000 0x1000`, `factory 0x10000 0x150000`.
  - `board_build.partitions = partitions.csv` in `platformio.ini`.
  - `CONFIG_PARTITION_TABLE_CUSTOM=y` and `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"` in `sdkconfig.defaults`, as the launcher does.
- Create: `apps/home_dashboard/main/readings.h`, `readings.c`, `test/test_readings.c`
- Modify: `apps/home_dashboard/main/CMakeLists.txt`: add `readings.c` to `SRCS`.
- Modify: `test/run.sh`: add a `readings` line with `-Iapps/home_dashboard/main`.

**Interfaces:**
- Produces, in `readings.h`:

```c
typedef enum { SENSOR_UPSTAIRS, SENSOR_DOWNSTAIRS, SENSOR_OUTSIDE, SENSOR_BATTERY, SENSOR_COUNT } sensor_t;
#define READING_STALE_US (300LL * 1000 * 1000)

typedef struct {
    bool available;         /* the last reply had a number for it */
    double value;
    int64_t reported_at_us; /* board time the sensor last reported to HA */
} reading_t;

typedef enum { BATTERY_IDLE, BATTERY_CHARGING, BATTERY_DRAINING, BATTERY_UNKNOWN } battery_direction_t;

/* The JSON body for POST /api/template; length, or -1 if size is too small. */
int readings_template_body(const char *const entity_ids[SENSOR_COUNT], char *out, size_t size);
/* Sets every reading from one reply; returns how many are available. */
int readings_apply(const char *reply, const char *const entity_ids[SENSOR_COUNT],
                   int64_t now_us, reading_t readings[SENSOR_COUNT]);
bool reading_stale(const reading_t *reading, int64_t now_us);
/* "66°" (UTF-8 degree sign), or "--" when unavailable. */
void format_temperature(const reading_t *reading, char *out, size_t size);
/* Magnitude only: "3.9", "153", "0.0" or "--". */
battery_direction_t format_battery(const reading_t *reading, char *out, size_t size);
```

- **Reply format:** each line is `<entity_id> <state> <age_seconds>`; the reply comes from Home Assistant's template endpoint.
  - The state is the text between the first and the last space.
  - A line is good when its age is a non-negative integer and its state parses fully with `strtod`.
  - `\r` before `\n` is ignored, and the last line may have no `\n`.
  - Any entity without a good line becomes unavailable.
  - `reported_at_us = now_us - age * 1000000`.
- **Stale:** `!available || now_us - reported_at_us > READING_STALE_US`.
- **Battery formatting:**
  - `|v| < 0.1` is idle with `"0.0"`.
  - Otherwise round to one decimal place. Below 100, show it with one decimal. At 100 or above, show whole amps with `lround(|v|)`.
  - Whole amps above 999 show `"--"` with `BATTERY_UNKNOWN`.
  - Otherwise the sign gives the direction: negative is `BATTERY_DRAINING`.
- **Temperature formatting:** `lround`, so `-0.4` gives `"0°"`, never `"-0°"`.

- [ ] **Step 1: Write `test/test_readings.c`.** The IDs are `{"sensor.a","sensor.b","sensor.c","sensor.d"}` and `NOW = 1000000000LL`.

```c
CHECK_STR(body, "{\"template\":\"{% for s in expand(['sensor.a','sensor.b','sensor.c','sensor.d']) %}"
                "{{ s.entity_id }} {{ s.state }} {{ (now() - s.last_reported).total_seconds() | int }}"
                "\\n{% endfor %}\"}");
CHECK(readings_template_body(ids, small, 20) == -1);
n = readings_apply("sensor.c 47.3 5\r\nsensor.a 64.2 14\nsensor.b unavailable 30\ngarbage\nsensor.d -3.876 0", ids, NOW, r);
CHECK(n == 3);
CHECK(r[0].available && r[0].value == 64.2 && r[0].reported_at_us == NOW - 14000000LL);
CHECK(!r[1].available);
CHECK(r[2].available && r[2].value == 47.3);
CHECK(r[3].available && r[3].value == -3.876);
n = readings_apply("sensor.a 64.2 14\n", ids, NOW, r);
CHECK(n == 1 && !r[3].available);  /* missing line clears it */
r[0] = (reading_t){true, 64.2, NOW - 299000000LL}; CHECK(!reading_stale(&r[0], NOW));
r[0].reported_at_us = NOW - 301000000LL;           CHECK(reading_stale(&r[0], NOW));
```

**Temperature table** (input, then expected): 64.2 → `"64°"`, 65.5 → `"66°"`, -0.4 → `"0°"`, -12.6 → `"-13°"`, 100.2 → `"100°"`, unavailable → `"--"`.

**Battery table** (input, then expected text and direction):

| Input (A) | Text | Direction |
|---|---|---|
| -3.876 | `"3.9"` | DRAINING |
| 90.52 | `"90.5"` | CHARGING |
| -0.05 | `"0.0"` | IDLE |
| 99.94 | `"99.9"` | CHARGING |
| 99.96 | `"100"` | CHARGING |
| -153.0 | `"153"` | DRAINING |
| 999.4 | `"999"` | CHARGING |
| 1075.1 | `"--"` | UNKNOWN |
| unavailable | `"--"` | UNKNOWN |

- [ ] **Step 2: Run `sh test/run.sh`.** Expected: FAIL, because `readings.h` is missing.
- [ ] **Step 3: Implement `readings.c`** with the C library only.
- [ ] **Step 4: Run `sh test/run.sh`.** Expected: `host tests passed`.
- [ ] **Step 5: Build `apps/home_dashboard`.** Expected: exit 0. The partition table summary shows a 0x150000 factory partition.
- [ ] **Step 6: Commit.** "Move the bird viewer to home_dashboard; parse Home Assistant readings"

### Task 5: Big font, generated and rendered (pure)

**Files:**
- Create: `tools/make_bigfont.py`
- Generate and commit: `apps/home_dashboard/main/bigfont_data.c`, `bigfont_data.h`
- Create: `apps/home_dashboard/main/bigfont.h`, `bigfont.c`, `test/test_bigfont.c`
- Modify: `apps/home_dashboard/main/CMakeLists.txt` (`SRCS`), `test/run.sh`

**Interfaces:**
- Produces, in `bigfont.h`:

```c
typedef struct {
    uint32_t code;     /* Unicode code point */
    uint8_t width, height;
    int8_t x_offset;   /* ink box left, from the pen position */
    uint8_t y_offset;  /* ink box top, from the line top */
    uint8_t advance;
    uint32_t offset;   /* into bitmap: 4-bit alpha, high nibble first, rows padded to a byte */
} bigfont_glyph_t;

typedef struct {
    uint8_t line_height, ascent;
    uint16_t glyph_count;
    const bigfont_glyph_t *glyphs; /* sorted by code */
    const uint8_t *bitmap;
} bigfont_t;

/* Pixels; unknown code points are 0 wide. UTF-8 input. */
int bigfont_text_width(const bigfont_t *font, const char *text);
/* Blends text into a width x rows row-major RGB565 buffer. x is the pen
   start in buffer columns; line_y (the line top) and first_row (the
   buffer's top row) share one row coordinate. Clips at the buffer's edges. */
void bigfont_render(const bigfont_t *font, const char *text, int x, int line_y,
                    uint16_t color, uint16_t *pixels, int width, int first_row, int rows);
```

- **Blending:** per RGB565 channel, `out = bg + (fg - bg) * alpha / 15`. Here `bg` is the pixel already in the buffer.
- **Produces, in `bigfont_data.h`:** `extern const bigfont_t` named `bigfont_label_480`, `bigfont_strip_480`, `bigfont_tile_480`, `bigfont_outside_480`, `bigfont_label_320`, `bigfont_strip_320`, `bigfont_tile_320` and `bigfont_outside_320`.

**The generator, `tools/make_bigfont.py`:**
- Run it as `uv run --no-project --with pillow --with matplotlib python tools/make_bigfont.py`.
- Source font: DejaVu Sans Bold, found with `matplotlib.font_manager.findfont(FontProperties(family="DejaVu Sans", weight="bold"), fallback_to_default=False)`. Name it and its licence (the Bitstream Vera/DejaVu licence) in the generated file's header comment.
- **Sizes in pixels** (the em, as passed to `ImageFont.truetype`):

  | Font | 480 board | 320 board |
  |---|---|---|
  | label | 20 | 14 |
  | strip | 48 | 32 |
  | tile | 72 | 48 |
  | outside | 120 | 80 |

- **Character sets:**
  - label: space, `A-Z`, `a-z`, `0-9`, `-`.
  - The other three: space, `0-9`, `.`, `-`, `°`, `A`, `▲`, `▼`.
- **Each glyph** is cropped to its ink box. Alpha is quantised with `round(v * 15 / 255)`.
- **`ascent`** comes from `font.getmetrics()[0]`. `line_height` is ascent plus descent.
- **`▲` and `▼`** are drawn as filled isosceles triangles, not taken from the font. Width is 0.9 times the advance of `0`, height is the ink height of `0`, they sit on the baseline, and their advance is the width plus 10% of the em.

- [ ] **Step 1: Write `test/test_bigfont.c`.**
  - `bigfont_text_width(&bigfont_tile_480, "88") == 2 * advance of '8'`.
  - The width of `"\xC2\xB0"` (°) and of `"\xE2\x96\xBC"` (▼) is above 0 in every non-label font.
  - The width of `"~"` is 0.
  - Render `"8"` into a buffer filled with `0x0000` using color `0xFFFF`: some pixel equals `0xFFFF`, and every pixel's channels lie between the bg and fg channels.
  - Render the same line in two strips (rows 0–9, then 10 to `line_height - 1`). The result equals one render of all rows.
  - Render at `x = -10` and at `x = width - 5` into a buffer with a sentinel word before and after. Both sentinels are untouched.
- [ ] **Step 2: Run `sh test/run.sh`.** Expected: FAIL, missing files.
- [ ] **Step 3: Write and run the generator, then implement `bigfont.c`.** Lookup is a binary search on `code`, and UTF-8 decoding covers 1–3-byte sequences.
- [ ] **Step 4: Run `sh test/run.sh`.** Expected: `host tests passed`. Then build `apps/home_dashboard`. Expected: exit 0, and `firmware.bin` grows by under 250 KB.
- [ ] **Step 5: Commit.** "Add a smooth digit and label font"

### Task 6: Glance layout and composition (pure)

**Files:**
- Create: `apps/home_dashboard/main/glance_layout.h`, `glance_layout.c`, `test/test_glance_layout.c`
- Modify: `apps/home_dashboard/main/CMakeLists.txt`, `test/run.sh`

**Interfaces:**
- Consumes: Task 4's `reading_t`, `format_temperature`, `format_battery` and `reading_stale`. Task 5's fonts and `bigfont_text_width`.
- Produces:

```c
typedef struct { int16_t x, y, w, h; } rect_t;
typedef struct {
    const bigfont_t *font;
    char text[16];
    int16_t x, line_y;  /* screen coordinates: pen start and line top */
    uint16_t color;
} glance_text_t;
typedef struct {
    rect_t rect;
    uint16_t background;
    int count;
    glance_text_t texts[4];
} glance_tile_t;

uint16_t glance_rgb(uint8_t r, uint8_t g, uint8_t b); /* RGB565 */
/* Zeroes tiles first, so whole tiles compare with memcmp. */
void glance_compose(int width, int height, const reading_t readings[SENSOR_COUNT],
                    int64_t now_us, glance_tile_t tiles[SENSOR_COUNT]);
```

**Rules:**
- **Rectangles at 480x320** (x, y, w, h):
  - upstairs: 4, 4, 212, 116
  - downstairs: 4, 124, 212, 116
  - outside: 220, 4, 256, 236
  - battery: 4, 244, 472, 72
- **Any other panel size:** scale x and w by `width/480` and y and h by `height/320`, rounding to the nearest pixel, and use the `_320` fonts. So 320x240 uses the `_320` fonts.
- **Colors:**

  | Element | Color |
  |---|---|
  | tile background | `#141414` |
  | labels | `#9A9A9A` |
  | inside numbers | `#FFD38A` |
  | outside number | `#7FD4FF` |
  | draining | `#FF8A7A` |
  | charging | `#8AE68A` |
  | idle, stale and `--` | `#707070` |

- **Temperature tiles:**
  - The label is centred horizontally, with its line top 6 px below the tile top (4 px on the 320 board).
  - The number is centred horizontally and vertically in the space below the label.
  - It uses the tile's own font, `outside` for the outside tile and `tile` for the others. If the number is wider than the tile minus 16 px, use the next smaller font: outside, then tile, then strip.
  - A stale reading draws its number in the stale gray.
- **Battery strip:** draw, left to right as one group centred in the strip, with 14 px gaps (9 px on the 320 board):
  - `SHOP BATTERY` in the label font, label gray;
  - the arrow and the number in the strip font: `▼153` when draining, `▲` when charging, no arrow when idle;
  - `A` in the label font, gray;
  - `draining`, `charging` or `idle` in the label font.
  - The number and the word take the direction's color, or the stale gray when stale or unknown.
  - All parts share one baseline, `line_y = baseline - font->ascent`, and the baseline is placed so the strip font's digits are vertically centred.

- [ ] **Step 1: Write `test/test_glance_layout.c`.**
  - The four 480x320 rectangles equal the values above.
  - At 320x240, upstairs is `{3, 3, 141, 87}` and battery is `{3, 183, 315, 54}`.
  - For both panel sizes, and for each worst case, every text's ink extent lies inside its tile rectangle. Measure it with `x .. x + bigfont_text_width` and `line_y .. line_y + line_height`. The worst cases:
    - temperatures `-12.6` and `100.2` in every temperature tile;
    - battery `-153.0` (draining) and `137.2` (charging);
    - every reading unavailable.
  - A stale upstairs reading has its number color equal to `glance_rgb(0x70, 0x70, 0x70)`.
  - Battery `-3.876`, fresh: the texts include `"▼3.9"` in `glance_rgb(0xFF, 0x8A, 0x7A)` and `"draining"`.
  - Battery `0.05`: the texts include `"0.0"` and `"idle"`, and none starts with an arrow.
- [ ] **Step 2: Run `sh test/run.sh`.** Expected: FAIL.
- [ ] **Step 3: Implement `glance_layout.c`.**
  - If the worst-case battery group can't fit with the sizes from Task 5, lower `strip` by 4 px in the generator and regenerate.
  - If a temperature can't fit even in `strip`, lower that board's `tile` and `outside` sizes the same way, rather than dropping any element.
- [ ] **Step 4: Run `sh test/run.sh`.** Expected: `host tests passed`.
- [ ] **Step 5: Commit.** "Lay out the glance screen"

### Task 7: Home Assistant, the glance screen and birds on the board

**Files:**
- Create: `apps/home_dashboard/main/ha_client.h`, `ha_client.c`, `glance.h`, `glance.c`
- Modify: `apps/home_dashboard/main/main.c`:
  - `load_config`;
  - the status page (`draw_status`, its strings, and `update_status_screen`'s photo conditions);
  - `refresh_birdnet` and `leave_bird_info`, which are replaced;
  - `displayed_species` and `displayed_image_ready`, which are removed;
  - the task loop.
- Modify: `apps/home_dashboard/main/CMakeLists.txt`: add the new files to `SRCS`.

**Interfaces:**
- Consumes:
  - Tasks 4–6.
  - From the viewer, unchanged: `fetch_latest_bird`, `fetch_bird_image`, `draw_jpeg`, `show_bird_info` and `report_failure`.
- Produces:
  - `esp_err_t ha_fetch(const char *base_url, const char *token, const char *body, char *reply, size_t capacity, int *http_status);`
    - POSTs to `{base_url}/api/template` with `Content-Type: application/json` and `Authorization: Bearer {token}`, and a 5 s timeout.
    - Copies the reply, NUL-terminated, into `reply`. A reply longer than `capacity - 1` is an error.
    - `*http_status` gets the HTTP status, or 0. The result is `ESP_OK` only on HTTP 200.
  - `void glance_draw(const glance_tile_t tiles[SENSOR_COUNT], bool full);`
    - Renders each tile in strips of up to 16 rows, through `cyd_draw_bitmap` from a `static uint16_t strip[CYD_LCD_MAX_WIDTH * 16]`.
    - Each strip starts as the tile background, then every text crossing it is blended in, at `x - tile.x`.
    - When `full` is false, a tile whose `glance_tile_t` equals the last one drawn (compared with `memcmp`) is skipped.
    - When `full` is true, the screen is cleared to black first.
  - `static bird_result_t show_latest_bird(void);` in `main.c`, returning `BIRD_SHOWN`, `BIRD_NONE` or `BIRD_FAILED`.
    - It fetches the latest visit, then streams its photo full screen.
    - On `BIRD_FAILED`, `server_status` holds the reason, set through `report_failure` as today.

**Settings and status:**
- `load_config` also reads `ha_url` (stripped of trailing `/`), `ha_token` and the four sensor keys, into static buffers of 128, 256 and 4×96 bytes.
- The request body is built once with `readings_template_body` into a 768-byte buffer. The reply buffer is 2048 bytes.
- **Status page rows** (row 0 becomes `HOME DASHBOARD`):

  | Row | Contents |
  |---|---|
  | 0 | title |
  | 1 | start status |
  | 2 | crash status |
  | 3 | `Wi-Fi <ssid>` |
  | 4 | Wi-Fi status |
  | 5 | `HA <host>` |
  | 6 | HA status |
  | 7 | hint |
  | 8 | RAM |
  | 9 | `tap to retry` |

- **HA status strings** (20 characters or fewer):
  - `no HA address`, `no HA token` or `no sensor IDs`, with the row 7 hint `use phone setup`;
  - `HA 401 bad token`;
  - `HA HTTP <code>`;
  - `HA <esp error without ESP_ERR_>`;
  - `no sensors found` when a 200 reply has none available;
  - `asking HA` while a request is out.
- **Row 7 otherwise** shows `EN opens launcher` while Wi-Fi is down, and is empty when it's up.

**Screen states**, an enum in `main.c`: `SCREEN_STATUS`, `SCREEN_GLANCE`, `SCREEN_PHOTO`, `SCREEN_INFO` and `SCREEN_MESSAGE`.

| Screen | Home Assistant fetch | On a tap | On its timer |
|---|---|---|---|
| STATUS | on Wi-Fi join, then every 30 s | fetch now | none |
| GLANCE | every 30 s | Clear to black and draw row 4 `finding today's` and row 5 `bird` straight away. Then: no Wi-Fi gives a MESSAGE of `NO WI-FI`; `BIRD_SHOWN` gives PHOTO; `BIRD_NONE` gives a MESSAGE of `NO BIRD YET` / `today at 80%+`; `BIRD_FAILED` gives a MESSAGE of `BIRDNET ERROR` / `<server_status>`. | none |
| PHOTO | none | `show_bird_info()`, then INFO | after 30 s, GLANCE |
| INFO | none | GLANCE | after 30 s, GLANCE |
| MESSAGE | none | GLANCE | after 5 s, GLANCE |

- **Leaving STATUS:** the first reply with at least one sensor available switches from STATUS to GLANCE, and STATUS never comes back.
- **Entering GLANCE** calls `glance_draw(..., true)`, and fetches at once if 30 s have passed since the last fetch.
- **While on GLANCE**, every loop iteration, at most once a second, runs `glance_compose` and then `glance_draw(..., false)`, so stale grays appear on time without a fetch.
- **A failed fetch** only logs and sets the HA status; the readings are left as they were.
- **Busy taps:** a tap that arrives while a photo or the details are loading is dropped, as the viewer's `ulTaskNotifyValueClear` does today.
- **The old refresh goes:** the 2-minute BirdNET refresh (`QUERY_INTERVAL_US`) is removed.

- [ ] **Step 1: Write `ha_client.c` and `glance.c`.** `ha_client.c` copies the viewer's event-handler pattern into a fixed buffer, and needs no new `PRIV_REQUIRES`.
- [ ] **Step 2: Change `main.c` as above.**
- [ ] **Step 3: Build.** Run `sh test/run.sh` and build `apps/home_dashboard`. Expected: both pass with no new warnings, and `firmware.bin` is at most 1,376,256 bytes.
- [ ] **Step 4: Check the glance screen on the board.**
  - Write `firmware.bin` to slot A (`0x150000`) with esptool, unsandboxed, and choose that slot in the launcher. Watch the serial monitor.
  - **Expected:**
    - The status page shows `asking HA`, then the glance screen in layout C with the user's real readings.
    - The serial log shows one POST every 30 s, each with HTTP 200.
    - The numbers match Home Assistant, give or take one refresh.
  - **Then** use phone setup to save a wrong token. **Expected:** after a restart, the status page shows `HA 401 bad token`.
  - **Finally** put the right token back.
- [ ] **Step 5: Check birds on the board.**
  - **Tap the glance screen.** "finding today's bird" appears within about 0.3 s, then the photo.
  - **Tap the photo.** The details page fills in.
  - **Tap again.** The glance screen returns with current numbers.
  - **Timeout:** with no tap, the photo and the details each return to the glance screen after 30 s.
  - **BirdNET unreachable:** temporarily set an unreachable BirdNET address on the phone page. `BIRDNET ERROR` shows for 5 s, then the glance screen returns. Restore the address afterwards.
- [ ] **Step 6: Commit.** "Show Home Assistant readings, with the latest bird on a tap"

### Task 8: Birds from wildlife-cam

Added on 2026-10-25 after the board check: the user doesn't run BirdNET-Go, so birds come from their wildlife-cam gallery's `GET /frame/wildlife` instead (spec, Birds). The brief for this task is self-contained; don't read other tasks.

**Files:**
- Create:
  - `apps/home_dashboard/main/json_scan.h`, `json_scan.c`: the viewer's hand-written JSON helpers, moved out of `main.c` unchanged, except for the string decoding below. Pure C: libc only.
  - `apps/home_dashboard/main/wildlife.h`, `wildlife.c`: reading a `/frame/wildlife` reply. Pure C: libc and `json_scan.h` only.
  - `test/test_wildlife.c`.
- Modify:
  - `apps/home_dashboard/main/main.c`;
  - `apps/home_dashboard/main/CMakeLists.txt` (`SRCS`);
  - `test/run.sh`: add the new test, with compile and run on separate lines (ruling R6);
  - `launcher/main/phone_setup.h`, `launcher/main/phone_setup.c`, `launcher/main/main.c`: the server field becomes the wildlife camera server.

**The reply, as served** (the sample has 12 birds; trimmed here):

```json
{"birds": [
  {"audio_url": "/audio/13621", "confidence": "46%", "label": "Common Raven", "photo_url": "/species_photo/comrav", "time": "5:21 PM"},
  {"audio_url": "/audio/13617", "confidence": "75%", "label": "Steller's Jay", "photo_url": "/species_photo/stejay", "time": "4:42 PM"}
 ],
 "sighting": {"camera": "water tower", "confidence": "74%", "image_url": "/image/13437", "label": "Mule deer", "thumb_url": "/thumb/13437", "time": "Oct 23, 10:52 PM", "ts": "2026-10-23T22:52:40.118551"},
 "sightings": [ … ]}
```

- `birds` is newest first, up to 12, one per species. `time` is preformatted: `5:21 PM` for today, `Oct 23, 10:52 PM` for an earlier day. `photo_url` may be `null`.
- The species photos sampled were baseline JPEGs, 330x220 to 330x499, 17–51 KB. A photo URL can also 404.
- Flask escapes non-ASCII as `\uXXXX`, but don't rely on it: raw UTF-8 must work too.

**Interfaces:**
- Consumes, from `main.c` as it stands: `http_get`, `http_response_free`, `report_failure`, `set_server_status`, `show_progress`, `set_stage`, `draw_jpeg`, `wrap_name`, `draw_lines`, `show_message`, `enter_timed_screen`, `enter_glance`, the `SCREEN_*` states, `server_status` under `status_lock`, and `strip_trailing_slashes`.
- Produces:
  - `json_scan.h`: `json_cursor_t`, `json_value_t` and the helpers `main.c` defines today: `json_skip_whitespace`, `json_parse_string`, `json_skip_value`, `json_object_field`, `json_value_string`, `json_value_number` and `json_object_string`, made non-static with the same names and behaviour.
  - `wildlife.h`:

    ```c
    #define WILDLIFE_MAX_BIRDS 12
    typedef struct {
        char label[48];
        char time[24];
        char confidence[8];
        char photo_url[96];
    } wildlife_bird_t;
    int wildlife_parse_birds(const char *json, size_t length, wildlife_bird_t *birds, int max);
    bool wildlife_join_url(const char *base_url, const char *path, char *out, size_t size);
    void wildlife_heard_row(const wildlife_bird_t *bird, int columns, char *out, size_t size);
    ```

**Rules:**
- **`wildlife_parse_birds`** returns the number of birds stored, from 0 to `max`, or -1 if the reply isn't a JSON object whose `birds` member is an array, or the array is cut off or malformed.
  - Any other top-level member is skipped whole, with `json_skip_value`: `sighting`, `sightings`, and anything added later.
  - Inside each bird, unknown members such as `audio_url` are skipped.
  - A bird whose `label` is missing, not a string, empty, or longer than 47 characters after decoding is left out. It doesn't count, and parsing goes on.
  - `time` or `confidence` that is missing, `null`, not a string or too long becomes an empty string, and the bird is kept.
  - `photo_url` that is missing, `null`, not a string, too long, or doesn't start with `/` becomes empty, meaning no photo.
  - It stops after `max` birds.
- **ASCII stand-ins in `json_parse_string`**, for every caller:
  - Decode `\uXXXX` escapes (a surrogate pair is one character) and raw UTF-8 sequences to code points.
  - U+2018, U+2019 and U+02BC become `'`; U+201C and U+201D become `"`; U+2013 and U+2014 become `-`; U+00A0 becomes a space.
  - Any other code point above 0x7E becomes a single `?`, and so does a malformed UTF-8 sequence.
  - The result is plain printable ASCII, which `cyd_text_row` can draw.
- **`wildlife_join_url`** writes `base_url` followed by `path` into `out`. It returns false, with `out` empty, if `path` doesn't start with `/` or the result doesn't fit.
- **`wildlife_heard_row`** writes the bird's label, then a space and its time if the whole still fits in `columns` characters. Otherwise it writes the label alone, cut to `columns`. An empty time gives the label alone.
- **Settings:**
  - `cyd_config` key `wildlife_url`, at most 127 characters. `load_config` reads it into a 128-byte buffer and strips trailing `/`. There is no default.
  - `birdnet_url`, `DEFAULT_BIRDNET_BASE_URL`, `GBIF_MATCH_URL` and `MIN_BIRD_CONFIDENCE` go.
- **Launcher phone page:**
  - The `BirdNET server` field becomes `Wildlife camera server`, with form name `wildlife_url`, and `phone_settings_t.birdnet_url` becomes `wildlife_url`.
  - It is still checked with `form_url_valid`. Its error reads `The wildlife camera server must be an http:// address.`
  - The launcher's `save_phone_settings` stores or erases `wildlife_url` as it did `birdnet_url`, and erases any old `birdnet_url` key. `load_config_string` reads `wildlife_url` back for the page.
  - Update the comment on `URL_MAX`, which names `birdnet_viewer`.
- **Removed from `main.c`:**
  - `bird_detection_t`, `bird_info_t` and `displayed_bird`;
  - `fetch_latest_bird`, `url_encode_path` and `fetch_bird_image`;
  - `get_json`, `json_top_number`, `json_top_string`, `fetch_family`, `fetch_weather` and `fetch_species_totals`;
  - `rounded`, if nothing else uses it;
  - the JSON helpers, which move to `json_scan.c`.
  - `TAG` becomes `"home_dashboard"`.
- **Photo stream:**
  - The second half of today's `fetch_bird_image` becomes `static esp_err_t open_image_stream(const char *url, esp_http_client_handle_t *client, size_t *length)`. It opens the URL, fetches headers, and requires HTTP 200 with a length from 1 to `IMAGE_JPEG_CAPACITY`.
  - It attaches the certificate bundle only for `https://`. Its log lines stop saying Wikimedia.
- **`draw_jpeg` keeps the aspect ratio.**
  - It fits the decoded image inside the screen, centred, on the black it clears to, instead of stretching it to fill.
  - Pick the decode scale as today: the smallest 1/2^n at which the decoded image is still at least as large as its fitted size in both directions.
  - A 330x220 photo still fills a 480x320 screen. A 330x499 one is 212x320, centred.
- **The tap on GLANCE:**
  - Draw rows 4 and 5 as `finding the` / `latest bird` straight away.
  - If there is no Wi-Fi, show a MESSAGE `NO WI-FI`. If `wildlife_url` is empty, show a MESSAGE `NO WILDLIFE CAM` / `use phone setup`.
  - Otherwise GET `{wildlife_url}/frame/wildlife` with `http_get`, capacity 8 KB. Parse it into `static wildlife_bird_t birds[WILDLIFE_MAX_BIRDS]` and a count, kept for the details page.
  - If the request fails or the reply can't be read, show a MESSAGE `WILDLIFE ERROR` / `server_status`. Set the reason through `report_failure` or `set_server_status`, as today; a parse failure says `bad reply`. An empty list shows a MESSAGE `NO BIRDS YET`.
  - Then stream `birds[0]`'s photo, joined to `wildlife_url`. Success goes to PHOTO. No photo URL, or a failure to open, fetch or decode, logs the reason and goes straight to INFO with the details page.
  - Progress stages: `getting birds`, `getting photo`, `drawing photo`.
- **PHOTO → tap → INFO → tap → GLANCE.** Each has its 30 s timer back to GLANCE, as today. The details page is drawn at once and makes no requests.
- **Details page rows:**
  - 0–1: the label, wrapped with `wrap_name`, in yellow.
  - 2: the time, in white.
  - 3: `confidence <confidence>`, in white, or empty if there is none.
  - 4: `also heard`, in gray, only if there is a second bird.
  - 5–8: `wildlife_heard_row` for `birds[1]` to `birds[4]`, in white, with `CYD_TEXT_COLUMNS`.
  - 9: `tap for readings`, in gray.
- `info_on_screen` and the status flags behave as Task 7 left them. Keep the busy-tap dropping.

- [ ] **Step 1: Write the failing test** `test/test_wildlife.c` with `test/check.h`, and add it to `run.sh`. Use only species names and made-up URLs, never anything household-specific. Cases:
  - **A three-bird reply shaped like the sample.** Include a `sighting` object, a `sightings` array of objects, and `audio_url` members. Returns 3, with every field checked.
  - **Stand-ins:**
    - `"Steller’s Jay"` gives `Steller's Jay`.
    - The same label in raw UTF-8 gives the same.
    - `"🐦 Wren"` gives `? Wren`.
    - `"Café Wren"` gives `Caf? Wren`.
  - **Skipped birds:**
    - A missing label, an empty one and a 48-character one are each left out, and the birds around them are kept.
    - A 47-character label is kept.
  - **Photo URL:**
    - `null` gives an empty `photo_url`.
    - `"http://x/y.jpg"` gives an empty one.
    - A 95-character path is kept, and a 96-character one gives empty.
  - **Missing fields:** `time` and `confidence` missing or `null` give empty strings, and the bird is kept.
  - **The `max` limit:** five birds with `max` 2 returns 2.
  - **Rejected replies:**
    - `[]`, `{}`, `{"birds": {}}`, and a reply cut off inside the array each return -1.
    - `{"birds": []}` returns 0.
  - **`wildlife_join_url`:**
    - `http://h:8080` plus `/species_photo/x` gives `http://h:8080/species_photo/x`.
    - A path without a leading `/` returns false.
    - A result one byte too long for `size` returns false, and one that fits exactly returns true.
  - **`wildlife_heard_row`:**
    - With 20 columns, `Common Raven` and `5:21 PM` gives `Common Raven 5:21 PM`.
    - `Chihuahuan Raven` and `5:02 PM` gives `Chihuahuan Raven`.
    - A 25-character label is cut to 20.
    - An empty time gives the label alone.
    - With 17 columns, `Common Raven` and `5:21 PM` gives `Common Raven`.
- [ ] **Step 2: Run** `sh test/run.sh`. Expected: it fails, because `wildlife.c` doesn't exist yet.
- [ ] **Step 3: Write `json_scan.c` and `wildlife.c`**, moving the helpers out of `main.c`. Run `sh test/run.sh`. Expected: pass.
- [ ] **Step 4: Change `main.c` and the launcher** as above.
- [ ] **Step 5: Build.**
  - Run `sh test/run.sh`.
  - Build `launcher` and `apps/home_dashboard` with `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run`.
  - Expected: all pass with no new warnings. Report both `firmware.bin` sizes; the dashboard's must be at most 1,376,256.
  - Run `git grep -n "birdnet_url\|BirdNET server" -- launcher apps`. Expected: no output.
- [ ] **Step 6: Commit.** "Show the latest bird from wildlife-cam"
- [ ] **Step 7: Board check, run by the controller with the user (ruling R3).**
  - Write the launcher's `firmware.bin` to `0x10000`, and the dashboard's to `0x150000`.
  - The user enters the gallery address on the phone page. The token and Wi-Fi password are left blank, so they're kept.
  - **Tap the readings:** `finding the latest bird` shows, then the newest bird's photo, aspect kept.
  - **Tap the photo:** the details show, with `also heard`.
  - **Tap again:** the readings come back.
  - **Timeouts:** the photo and the details each go back to the readings after 30 s.
  - **Gallery unreachable:** stop wildlife-cam's gallery, or enter a wrong address. `WILDLIFE ERROR` shows for 5 s.

### Task 9: Documentation and release

**Files:**
- Modify:
  - `apps/home_dashboard/README.md`: rewrite it for the dashboard. Cover what it shows, the settings it reads and the phone page (including the admin-token requirement), birds on tap from wildlife-cam's `/frame/wildlife`, and the standalone partition.
  - `README.md` and `docs/INSTALL.md`: replace `birdnet_viewer` with `home_dashboard`. INSTALL section 5 gains the Home Assistant fields and the wildlife camera server, where to create a token, and that the token must belong to an admin user, because `POST /api/template` is admin-only and a non-admin token gets `HA 401 bad token` (ruling R7). Replace every mention of the BirdNET server setting (`birdnet_url`) with `wildlife_url`.
  - `CLAUDE.md`, `.github/copilot-instructions.md` and `launcher/README.md`: wherever `birdnet_viewer` is named. `CLAUDE.md`'s settings bullet and `launcher/README.md`'s settings table also swap `birdnet_url` for `wildlife_url`.
  - `launcher/main/main.c`: comments that name `birdnet_viewer`.

- [ ] **Step 1: Edit.** Then run `grep -rn "birdnet_viewer" --exclude-dir=.git --exclude-dir=superpowers .`. Expected: no output.
- [ ] **Step 2: Final checks.**
  - `sh test/run.sh` passes.
  - The launcher and `apps/home_dashboard` both build.
  - `git grep -nE "near_|main_shop|eyJ"` gives no output. This catches entity IDs and tokens, which start `eyJ`.
- [ ] **Step 3: Commit.** "Document the home dashboard"
- [ ] **Step 4: Ask the user before pushing `main` to `origin`.** After the push, check CI with `gh run list --repo rylincoln/cheap-yellow-board --limit 1` until it shows `completed success`. Then check that `gh release list` shows `home_dashboard`.
- [ ] **Step 5: Delete the old releases, only after a separate yes in chat.** The nine are `birdnet_viewer`, `CatMouse`, `Datalogger`, `bluetooth_diag`, `gear_roll`, `screen_check`, `stopwatch`, `touchscreen_cal` and `wifi_checker`. Delete each with `gh release delete <tag> --repo rylincoln/cheap-yellow-board --yes --cleanup-tag`. Then the launcher lists only `home_dashboard`.
