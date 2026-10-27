# Weather dashboard for a second board: design

A new boot image, `apps/weather_dashboard` *(designer's call: the name)*, for the user's second 3.5" board, which they are giving to their parents **(user)**. It shows the outdoor readings from the user's weather station and the shed battery's charge, all read from the user's Home Assistant **(user)**.

Marks: **(user)** means the user said or confirmed it. *(designer's call)* means a choice made here, which the user can overrule.

## Why

The user's parents live on the same property and the board sits on the user's home network **(user)**. They want to see the weather outside and how the shed battery is doing without asking or opening an app. The user's own board runs `apps/home_dashboard`, which has the indoor temperatures, the shop battery and the birds. That screen isn't what the parents need.

## What the parents see

One screen, layout A of the mockups **(user picked)**, in the same style as the home dashboard: dark tiles (`#141414`) on black, bold DejaVu Sans numbers, gray (`#9A9A9A`) labels in capitals. Positions are on the 480x320 grid and scale to the 2.8" panel the way `glance_layout.c` does *(designer's call)*.

| Tile | Rect (x, y, w, h) | Contents |
|---|---|---|
| Outside | 4, 4, 288, 236 | `OUTSIDE`; the current temperature, large, blue `#7FD4FF`; under it `▲58°` in amber `#FFD38A` and `▼39°` in blue for the high and low over the past 24 hours; `PAST 24 HOURS` small |
| Rain today | 296, 4, 180, 116 | `RAIN TODAY`; inches to two decimals, then a small gray `in` |
| This week | 296, 124, 180, 116 | `THIS WEEK`; the same |
| Shed | 4, 244, 472, 72 | `SHED`; a battery icon filled to the charge; the charge and a small gray `%`; a gray `→`; a sunrise icon; the forecast charge at dawn in amber and `%`; `AT DAWN` small. One line, centred |

The mockup is in `.superpowers/brainstorm/36814-1793048100/content/layout.html`, option A.

### Readings and formats

- **Temperatures** use the home dashboard's rules: whole degrees and `°`, `--` when unavailable or 999.5 or more either way. The big number steps down a font size when it gets too wide (three digits or a minus sign), as Outside does on the home dashboard *(designer's call)*.
- **Rain** shows two decimals. A dry `0.00` is gray rather than white *(designer's call)*. Values of 10 or more can show one decimal; the builder picks what fits the tile.
- **Battery charge and dawn forecast** use the home dashboard's `format_percent`: whole percent, `--` outside -0.5 to 100.5. The battery icon is filled in tenths, like the shop battery strip. Either number turns red `#FF8A7A` at 20% and under *(designer's call)*. Otherwise the charge is light gray `#E6E6E6` and the forecast is amber.
- **Units.** The board shows Home Assistant's numbers as they come, with `°` and `in`, and converts nothing. The user's weather station already reports °F and inches in Home Assistant *(checked there on 2026-10-26)*.

### Old or missing data (user confirmed)

- Only the live readings turn gray when Home Assistant hasn't heard from the sensor for 5 minutes: the current temperature and the battery charge.
- The high and low, both rain totals and the dawn forecast never turn gray. They can rightly stay unchanged for hours.
- A reading Home Assistant can't supply shows `--`. When one is missing, the battery icon is empty and gray.

### Updating

The screen refreshes every 30 seconds, as the home dashboard does, and redraws only the tiles that changed *(designer's call)*. Until the first good reply, it shows the home dashboard's status page with the same messages: `no HA address`, `no HA token`, `no sensor IDs`, `HA 401 bad token`, `HA HTTP <code>`, `no sensors found`.

### Touch

Nothing happens on a tap *(designer's call, shown to the user without objection)*. There are no birds and no other screens.

## Where the data comes from

- **Home Assistant does the work (user).** Every value is a Home Assistant entity. The board never keeps history or computes anything.
- **What exists** *(checked in Home Assistant on 2026-10-26)*. The weather station has sensors for the current temperature, rain today and rain this week, all in °F and inches. The shed's own shunt has a charge sensor in percent, and there's a projected dawn charge sensor. **No 24-hour high or low exists yet.** Two statistics sensors (max and min of the station's temperature over 24 hours) are made in Home Assistant before the board can be checked end to end **(user: HA can be made to do it)**.
- **Seven sensors (user confirmed):** current temperature, 24-hour high, 24-hour low, rain today, rain this week, shed battery charge, and shed battery charge forecast at dawn. Checking the board end to end needs all seven to exist in Home Assistant first.
- **One request.** The board reads them with the same single `POST /api/template` the home dashboard uses, with the same `ha_url` and an admin `ha_token` *(designer's call)*. The board is on the user's network **(user)**, so no remote access or HTTPS is needed.

## Settings

Settings stay in NVS namespace `cyd_config`, written by the launcher's phone page. No entity ID, token or address goes into code, docs or a binary.

- **The phone page gets a second section of seven fields (user).** The user doesn't type them on a phone: while phone setup runs, they open the board's LAN address from their laptop and paste the IDs. They already do this for the home dashboard, and it works **(user)**. The page is reachable there because phone setup runs the access point and the Wi-Fi station together.
- **A new board's flow.** It first joins the home Wi-Fi through the launcher's network chooser or phone setup. Then the user opens phone setup again and pastes the address, token and IDs from the laptop. A blank password keeps the saved one while the network stays the same, and a blank token keeps the saved one while the Home Assistant address does, so a settings-only save doesn't retype them. No launcher change is needed for this.
- **Keys** *(designer's call; NVS keys are at most 15 characters)*: `ha_wx_temp`, `ha_wx_high`, `ha_wx_low`, `ha_rain_today`, `ha_rain_week`, `ha_shed_pct`, `ha_shed_dawn`.
- **Sections.** The page labels the two groups of sensor fields by the app that uses them, such as "Home dashboard" and "Weather dashboard". Both groups always show, and each board fills only its own *(designer's call)*.
- **Required.** The weather dashboard needs all seven, like the home dashboard's required four, and shows `no sensor IDs` until they're saved *(designer's call)*.
- The phone page's form buffer (`FORM_MAX`) grows to hold every field percent-encoded.

## Constraints

- It's a normal app under `apps/`: CMake `project()` named after the folder, `cyd_display` and `cyd_update` included, built and released by CI with no workflow change.
- **Updates come the same two ways (user, from the LAN updates work).** `pio run -t upload` from the Mac over the network, and GitHub releases installed by the board itself. The board checks only the `launcher` release and its own app's release.
- No PSRAM and about 320 KB of free DRAM. No full frame buffer; tiles draw in strips as the home dashboard's do.
- The home dashboard must keep working and keep its look. It changes only where shared code moves.
- Plain-C parts (formatting, layout, request body, reply parsing) get host tests in `test/run.sh`, as the home dashboard's do.

## Non-goals

- Indoor readings, the shop battery, birds or any tap action.
- Forecasts beyond the dawn battery charge, wind, humidity, pressure, or anything else from the weather station.
- Reaching Home Assistant from outside the home network.
- Unit conversion on the board.
- Dimming at night. Nobody asked for it; the launcher's brightness setting still applies.
- Changing the launcher on the parents' board **(user)**. It keeps its power-on countdown and menu, which also offers the home dashboard.

## Left to the builder

- **How to share code with the home dashboard.** The home dashboard's `main.c` mixes Wi-Fi, the status page, the Home Assistant fetch, the update hooks and the birds. Moving the shared parts into a component under `components/` is preferred over copying them *(designer's call)*, but the builder chooses the split. Note that CI rebuilds every app when `components/` changes, which is harmless.
- **Fonts.** `tools/make_bigfont.py` writes the home dashboard's fonts. The weather dashboard needs its own glyph set: digits, `.`, `-`, `°`, `▲`, `▼`, `→`, the battery icons, a sunrise icon, `%` and the label letters. Whether the generator takes per-app arguments or a second script is the builder's call, as long as each app keeps a pinned, reproducible command.
- **Font sizes, gaps and exact icon drawings**, within the layout above.
- **Setup steps for the second board** in the app's README and `docs/INSTALL.md`: flash the launcher over USB, join Wi-Fi, fill in the settings, then install the app from the launcher's menu or push it.
- **Docs:** the new app's README, the settings lists in `CLAUDE.md`, `launcher/README.md` and `docs/INSTALL.md`.
