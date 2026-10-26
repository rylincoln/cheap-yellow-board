# Home dashboard: design

A boot image, `apps/home_dashboard`, that replaces `apps/birdnet_viewer` **(user)** and turns the board into a glance display for the house: three temperatures and the Shop battery current from Home Assistant, with the latest bird heard one tap away (from the user's wildlife-cam gallery since 2026-10-25; originally BirdNET). It becomes the board's only app **(user)**: the other eight are removed.

Decisions the user made are marked **(user)**. Everything else is the designer's call and can be revisited.

## Why

Interesting data already exists on the local network: Home Assistant at `http://192.168.1.33:8123/` and wildlife-cam, which identifies birds by song and serves them on the LAN (the spec first named a BirdNET-Go server, which the user turned out not to run). Walking past the board, the user wants to read the temperatures inside and out and see how the Shop battery is doing without opening a phone. Birds stay available as a tap, since the board already knows how to show them.

## What the user sees

### Glance screen (the default)

Layout C **(user)**, chosen from three to-scale mockups. On the 3.5" board (480 x 320), with 4 px margins and gaps:

| Element | Position (x, y, w, h) | Content | Color |
|---|---|---|---|
| Upstairs tile | 4, 4, 212, 116 | label `UPSTAIRS`, whole degrees, e.g. `66°` | amber `#FFD38A` |
| Downstairs tile | 4, 124, 212, 116 | label `DOWNSTAIRS`, e.g. `64°` | amber |
| Outside tile | 220, 4, 256, 236 | label `OUTSIDE`, the largest number, e.g. `46°` | blue `#7FD4FF` |
| Battery strip | 4, 244, 472, 72 | `SHOP BATTERY`, arrow, amps (see Range below), `A`, then `draining` or `charging`, on one line | red `#FF8A7A` draining, green charging |

- Tiles are dark gray (`#141414`) on black, and labels are gray (`#9A9A9A`) caps.
- Numbers are as large as the tiles allow: digits roughly 70 px tall in the left tiles, roughly 100 px in the outside tile, and roughly 48 px in the strip.
- The mockup is kept at `.superpowers/brainstorm/39682-1792950806/content/glance-layout-v2.html`, which is git-ignored and local to this machine.
- The degree sign stands alone, with no `F`.

### Sensors **(user)**

| Label | Home Assistant sensor | Display |
|---|---|---|
| UPSTAIRS | remote temperature channel of the Ambient Weather station | rounded to whole degrees |
| DOWNSTAIRS | the Ambient Weather console's inside temperature | rounded to whole degrees |
| OUTSIDE | the Ambient Weather outdoor temperature | rounded to whole degrees |
| SHOP BATTERY | the Shop battery's Victron shunt current | magnitude, see Range below |

- Home Assistant reports the temperatures in °F, and the battery current in amps.
- The entity IDs are typed in on the phone setup page **(user)**. The repository and the public release binary never contain them, because they include a place name.
- The user identified them with the designer on 2026-10-25, and will enter them on the board.
- The four labels are fixed in the app.

### Battery direction

Negative current means the battery is draining, which is Victron's convention **(user confirmed)**:

- **Draining** (negative): a down arrow `▼` and the word `draining`, in red.
- **Charging** (positive): an up arrow `▲` and the word `charging`, in green.
- **Idle** (designer's call): when the magnitude is below 0.1 A, show `0.0 A idle` in gray with no arrow.

**Range.** Home Assistant's statistics for this sensor (since 2026-08-07) show peaks from −153 A to +137 A, with hourly averages under 92 A, plus one glitch reading of 1075 A. Three-digit currents are routine at peaks, so (designer's call):
- below 100 A, show one decimal (`3.9`);
- from 100 A, show whole amps (`153`);
- above 999 A, show `--` as a glitch.

The strip has to fit `▼153 A`. The builder may drop the strip's words to the grid font at 2x to make room.

### Freshness **(user confirmed the intent)**

- Readings are fetched every 30 seconds.
- If Home Assistant can't be reached, the last numbers stay on screen.
- A number turns gray, so a stale reading never passes for a current one, in either case (designer's call):
  - the board hasn't fetched it successfully for 5 minutes;
  - the sensor itself hasn't reported to Home Assistant for 5 minutes, judged by its `last_reported` time. HA keeps serving a dead sensor's last value without marking it unavailable, so a successful fetch alone doesn't prove the reading is current.
- `last_updated` and `last_changed` are the wrong clocks. On 2026-10-25 the downstairs sensor had reported 14 s earlier while its `last_updated` was 18 minutes old, because its value hadn't changed.
- The Ambient sensors report about once a minute and the Victron shunt every few seconds, so one 5-minute limit suits all four.
- An entity whose state is `unavailable` or `unknown` shows `--` in gray.

### Birds **(user confirmed; source changed on 2026-10-25, see below)**

The sequence is: glance screen → tap → bird photo → tap → bird details → tap → glance screen.

- With no tap for 30 seconds, the photo or the details go back to the glance screen.
- **The source is the user's wildlife-cam gallery (user, 2026-10-25), not a BirdNET-Go server.** The user doesn't run BirdNET-Go. wildlife-cam runs BirdNET on its camera audio and serves the result on the LAN as `GET /frame/wildlife`, which needs no login:
  - The reply is JSON: `{"sighting": …, "sightings": […], "birds": […]}`.
  - `birds` holds up to 12 recent birds heard, newest first, one per species.
  - Each bird has `label` (such as `Steller's Jay`), `time` (preformatted, `5:21 PM` today or `Oct 23, 10:52 PM` for an earlier day), `confidence` (preformatted, such as `46%`), `photo_url` and `audio_url`.
  - `photo_url` is relative to the gallery, such as `/species_photo/comrav`. It serves a Wikipedia species photo as a baseline JPEG, about 330 px wide and 17–51 KB in the samples taken. It may 404, in which case wildlife-cam's own page falls back to a spectrogram.
- **The photo:** the newest bird (`birds[0]`) **(user)**. Its species photo is streamed full screen, scaled to fit with its aspect ratio kept and centred on black (designer's call; the viewer stretched photos to fill).
- **The details page (designer's call):**
  - the bird's name, wrapped over two rows;
  - its time, and its confidence;
  - under `also heard`, the next birds in the list, one per row, as many as fit. Each row is the name, then the time when the whole row still fits.
  - The viewer's family, weather, call count and species totals came from BirdNET-Go and are dropped. So is the 0.80 confidence floor: wildlife-cam has already filtered what it lists.
- **No usable photo:** if the photo can't be fetched or decoded (a 404, a PNG, a progressive JPEG, too large), the tap goes straight to the details page (designer's call).
- The gallery is only queried on a tap. It is not polled in the background (designer's call).
- A tap must visibly respond within a fraction of a second, because the photo can take seconds to stream.
- **Messages, shown for about 5 seconds before the glance screen returns (designer's call):**
  - no gallery address saved: `NO WILDLIFE CAM` / `use phone setup`;
  - an empty `birds` list: `NO BIRDS YET`;
  - the request failed or the reply couldn't be read: `WILDLIFE ERROR` / the reason;
  - no Wi-Fi: `NO WI-FI`.
- Labels can hold characters the board's 6x8 font lacks, such as a curly apostrophe. They are shown with ASCII stand-ins (designer's call).

### Before the first readings

- Until the first successful Home Assistant read, the screen shows a status page like `birdnet_viewer`'s:
  - why the board started (including crash, watchdog or brownout resets);
  - the Wi-Fi network and why joining fails;
  - the Home Assistant address and its last result, such as `HA 401: bad token` or `no HA token: use phone setup`;
  - free RAM.
- A tap on the status page retries at once.
- Once readings arrive, the status page doesn't come back unless the board restarts. Later failures show only as stale (gray) numbers and in the serial log.

### Being the default app

- Nothing new is needed. On power-on the launcher counts down for 20 seconds to the last image that ran, so after the user starts the dashboard once it comes back after every power cut.
- Any other reset, such as a crash, restarts the dashboard directly.
- Changing the launcher's countdown is out of scope.

## Settings

The Home Assistant address, token and four entity IDs live in the board's NVS like the other shared settings, in the `cyd_config` namespace. No binary carries them **(user)**.

| Key | Contents |
|---|---|
| `ha_url` | e.g. `http://192.168.1.33:8123`; no default |
| `ha_token` | A Home Assistant long-lived access token, about 180 characters. The user creates it at the bottom of their HA profile page, under Security. **It must belong to an admin user:** Home Assistant's `POST /api/template` is admin-only and answers a non-admin token with 401 (found on the board, 2026-10-25). |
| `ha_upstairs`, `ha_downstairs`, `ha_outside`, `ha_battery` | Entity IDs, such as `sensor.some_name` |
| `wildlife_url` | The wildlife-cam gallery, such as `http://192.168.1.50:8080`; no default. It replaces `birdnet_url`, which is no longer read or written (2026-10-25). |

If any of these is missing, the status page names what's missing and says to use phone setup.

**Where they're entered:**
- The launcher's phone setup page gets a `Home Assistant` section under the server field, with six fields:
  - address;
  - token;
  - an entity ID for each of Upstairs, Downstairs, Outside and Shop battery.
- They behave like the existing server field: blank erases the setting.
- The BirdNET server field becomes `Wildlife camera server` and stores `wildlife_url` (2026-10-25). It is validated as an `http://` address, as the BirdNET field was.
- The token is the exception:
  - When the address is unchanged, a blank token keeps the saved one, as the Wi-Fi password does.
  - The token is never shown back on the page.
- The address and the entity IDs are shown back, so they can be edited.
- Entity IDs are checked for the `domain.object_id` form (lowercase letters, digits and underscores, one dot), as the page already checks the Wi-Fi password.
- The on-device keyboard does not get these fields, because typing a 180-character token on a resistive screen isn't realistic.
- The POST body limit in `phone_setup.c` (`FORM_MAX`, 768 today) must grow to fit the new fields percent-encoded. The save callback and `phone_setup_config_t` grow to carry them.

**Getting the new phone page onto the board:**
- The launcher is not published by CI; it is flashed over USB.
- To keep the saved Wi-Fi and calibration, write only the launcher's `firmware.bin` to `0x10000`, without `erase_flash`.
- The README needs to tell other users this.

## How it's made

- **Platform:** a PlatformIO / ESP-IDF C project in `apps/home_dashboard`, grown from the viewer as below, using `components/cyd_display` like every other app. `project(home_dashboard)` must equal the folder name; CI then builds it and publishes it as release `home_dashboard` with no workflow change.
- **Home Assistant:**
  - Plain HTTP on the LAN, one request per refresh: `POST {ha_url}/api/template` with `Authorization: Bearer {ha_token}`.
  - The template returns each entity's state and the seconds since its `last_reported`. The board has no clock (no project uses SNTP), so the age must come from HA.
  - This template was tried against the user's HA on 2026-10-25 and works:
    `{% for s in expand([...the four ids...]) %}{{ s.entity_id }} {{ s.state }} {{ (now() - s.last_reported).total_seconds() | int }}\n{% endfor %}`
    `expand()` returns the entities sorted by ID, not in list order, so match lines by entity ID.
  - Keep a hard cap on the response size, as for every HTTP response in the repo.
- **Grown from the viewer:** the dashboard replaces `birdnet_viewer` **(user)**.
  - Start with `git mv apps/birdnet_viewer apps/home_dashboard`, so the viewer's history carries over.
  - Then rename `project()` and `build_dir`, and build the glance screen and Home Assistant client on top.
  - The JPEG streaming and status page stay; the 2-minute BirdNET polling becomes tap-only. The BirdNET-Go fetches and details were later replaced by wildlife-cam (see Birds).
  - Update every mention of `birdnet_viewer`: the root README, `docs/INSTALL.md`, `CLAUDE.md`, the Copilot notes, the app's own README, and the phone page's BirdNET field, which says `for the BirdNET viewer`.
- **Big digits:**
  - The text grid's 5x7 font scaled up looks too blocky at 100 px.
  - Use a dedicated digit font covering only `0-9 . - ° ▲ ▼ A` and the space, anti-aliased against the tile color, at the two or three sizes above.
  - Render it through `cyd_draw_bitmap` in strips, with no frame buffer.
  - Labels and the `draining`/`charging` word can use the existing grid font at 3x.
  - The builder picks how to generate the glyph data, for example a script that rasterises a TTF into a C array, checked in alongside its output.
- **The only app (user):** remove the other eight folders under `apps/`: `CatMouse`, `Datalogger`, `bluetooth_diag`, `gear_roll`, `screen_check`, `stopwatch`, `touchscreen_cal` and `wifi_checker`.
  - Git history keeps them.
  - Keep the launcher: it still runs Wi-Fi and phone setup, and downloads dashboard updates into its slots. Simplifying it for a single app is out of scope.
  - Update every mention of the removed apps:
    - the app tables in the root README and `docs/INSTALL.md`;
    - the `wifi_checker` line in `launcher/README.md`'s settings table;
    - the `CatMouse` example in `.github/workflows/build.yml`'s comment;
    - the `touchscreen_cal` comment in `cyd_display.h`;
    - `CLAUDE.md` and the Copilot notes.
  - Their releases on the fork, and `birdnet_viewer`'s, must be deleted, or the launcher keeps listing them. Deleting releases is the user's go-ahead, after the `home_dashboard` release exists.

## Constraints

- **Flash:** the app must fit a launcher slot, 1,376,256 bytes, which CI enforces. The viewer it grows from is about 1.04 MB today. That rules out LVGL here: the stopwatch and gear_roll builds show LVGL adds roughly 300 to 370 KB over a plain app.
- **Standalone partition:** the app will also outgrow the 1 MB factory partition that PlatformIO's default table gives a standalone (`pio run`) build. It needs its own `partitions.csv` with a larger factory partition, selected by `board_build.partitions` in `platformio.ini`.
- **RAM:** about 320 KB free, no PSRAM. No frame buffer; the photo streams as in the viewer. Wi-Fi plus TLS during the photo fetch is the peak.
- **Other boards:** the 2.8" board (320 x 240) gets the same layout scaled by 2/3. It is untested on hardware, and the 3.5" board is the one that matters.
- **Repository rules** (see `CLAUDE.md`): no credentials in any binary, and buffers sized with the `CYD_*_MAX_*` constants.

## Not in scope

- The Reolink cameras.
- Humidity, history graphs and trends.
- Any Home Assistant control (switches, scenes).
- Alerts.
- Choosing sensors or labels on the board's own screen.
- Night dimming.
- Changes to the launcher beyond the Home Assistant section of the phone page.

## Would make it a failure even if it works

- A number that looks current but is old.
- A screen too busy to read from across the room.
- A token, password or household entity ID in a published binary or in the repository.
- Breaking the launcher's existing Wi-Fi setup.

## Left to the builder

- Exact font sizes and baselines within the layout.
- The glyph generation tool.
- The wording of status and one-line messages.
- The task structure, as long as taps and Wi-Fi events wake the app as they do in the viewer.
