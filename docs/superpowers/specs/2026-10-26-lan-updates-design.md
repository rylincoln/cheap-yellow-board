# Updates over the network: design

The board updates itself without a USB cable or a tap. A build pushed from the Mac, or a new release on GitHub, gets installed and the board restarts into it **(user)**. USB stays as the way to recover from anything **(user)**.

Marks: **(user)** means the user said or confirmed it. *(designer's call)* means a choice made here, which the user can overrule.

## Why

The board now lives on the Home Assistant machine's USB port for power, away from the Mac. Today a new build reaches it in one of two ways, and both need someone at the board:

- **USB.** Carry the board to the Mac and flash it with esptool.
- **The launcher's GitHub menu.** It only runs for 20 seconds after power-on, and needs a tap.

The user wants to "simply push an update anytime" and have the board "refresh/reboot itself" **(user)**.

## What the user gets

### Two ways an update arrives (user)

1. **From the Mac.** Running `pio run -t upload` in `launcher/` or in an app folder sends the build over the network. The board installs it and, for an app, restarts into it. This is for trying changes before they go to GitHub *(designer's call: the command)*.
2. **From GitHub.** After a `git push` to `main`, CI publishes releases as it does today. The board notices the new release by itself and installs it. Nobody taps anything.

### What can be updated this way (user)

"As much as possible", by both routes:

- **The running app**, today `home_dashboard`.
- **The launcher.** A bad launcher update is fixed over USB **(user)**.
- **Not the bootloader or the partition table** *(designer's call)*. They stay USB-only: they almost never change, and a bad write there costs more than a bad launcher.

### Test builds pause GitHub (user confirmed)

When a build arrives from the Mac, the board stops installing that image from GitHub. It resumes with the first release of that image built from a later commit than both:

- the commit the test build was made from, and
- the commit of the release that was current when the test build arrived.

In practice that's the release CI publishes after the change is committed and pushed *(designer's call: what "newer" means)*. So:

- A CI run that was already going when the test build arrived doesn't end the pause. Its commit is older than the test build's.
- A test build made from an old checkout doesn't get replaced by the release it was compared against.
- A push that doesn't touch that image doesn't end the pause either, because CI no longer republishes images that didn't change (see "How it works").

The pause is per image: pushing a test dashboard doesn't pause launcher updates, and the other way round *(designer's call)*.

**A build flashed over USB pauses GitHub the same way** *(designer's call, flagged for the user)*. Otherwise the next hourly check would put back the release that a USB flash just replaced, and undo a USB recovery of a bad launcher.

### A broken build doesn't strand the board (user confirmed the intent)

- **A new app build** must report one good start: it has run for 60 seconds and drawn its first screen *(designer's call: the definition)*. If it crashes or restarts before that, the board goes back to the build it had before.
- **A release that fails its good start isn't installed again.** The board waits for a release built from a later commit *(designer's call)*. Without this, the old build would reinstall the bad release at its next check, forever.
- **The previous build stays in the other slot** until the next update replaces it.
- **A bad launcher** is fixed over USB, as today **(user)**. Until then the board keeps running its app:
  - a launcher that crashes restarts into the app, because only power-on goes to the launcher;
  - a launcher write that was cut off fails its check at power-on, and the bootloader starts the app instead (see "How it works");
  - a launcher that hangs without crashing stops the board at power-on until it's fixed over USB.

### What the screen shows *(designer's call)*

- **While an update downloads and installs:** a full-screen `UPDATING` line, with what it is, where from, and a percentage. For example `dashboard from Mac 45%`, or `launcher from GitHub 80%`.
- **After an app update:** a brief `RESTARTING`, then the board restarts into the new build.
- **After a launcher update:** `LAUNCHER UPDATED` for a few seconds, then back to the readings. There is no restart, because the new launcher runs at the next power-on.
- **When a GitHub install is due:** it waits until the board is on the readings or status screen, not mid-photo. A push from the Mac is installed straight away.

## Security

- **The board only accepts a pushed build that proves it knows a secret.** Without one, anyone on the Wi-Fi could replace the firmware.
  - The secret is set on the launcher's phone setup page, in a new `Update key` field, and stored in NVS `cyd_config` as `update_key`.
  - Like the token, it's never shown back.
  - With no key saved, the board refuses every push from the Mac. GitHub updates still work.
- **The key is a random one the Mac makes** *(designer's call, flagged for the user)*.
  - A script in `tools/` makes 20 random characters in groups of four, from an alphabet without look-alike characters: about 100 bits.
  - It saves the key to `~/.config/cyd/update_key`, mode 600, and copies it to the clipboard. It prints it only when asked, so the user can type it on a phone.
  - The user pastes or types it into the `Update key` field once.
  - Why not a passphrase the user picks: anyone who records one push on the Wi-Fi can test guesses offline, as fast as their computer allows. A chosen passphrase may not survive that; 100 random bits will.
- **The key never travels, and nothing is erased before the push is proven** *(designer's call)*.
  1. The Mac asks the board for a one-time challenge.
  2. It sends the build with an HMAC-SHA256, keyed with the secret, over the challenge, the build's length, its SHA-256 and its project name.
  3. The board checks that HMAC before it erases anything. A push without the key can't wipe the backup slot or the launcher.
  4. The board computes the SHA-256 of the bytes as they arrive, and only switches to the new image if it matches.
  5. A challenge works once and expires after a minute, so replaying a recorded push fails.
- **The key on the Mac** stays in that file. It is never committed, printed unasked, or put on a command line.
- **GitHub updates come only from the launcher's `IMAGE_REPO`** over HTTPS with the certificate bundle. Only release tags matching the running app's project name, or `launcher`, are installed.

## How it works (decided parts)

### On the board

- **The running app does the work.** The launcher only runs for 20 seconds after power-on, and nobody power-cycles the board. So:
  - the app runs a small HTTP server for pushes;
  - it advertises itself by mDNS as `cyd-xxxx.local`, where `xxxx` is the last four hex digits of its Wi-Fi station MAC address;
  - it checks GitHub at start and then every hour *(designer's call)*.
- **Put it in a shared component,** `components/cyd_update`, so any future app gets it by including the component and calling one start function *(designer's call)*. `home_dashboard` is the only user today.
- **Which image is it.** The board reads the pushed image's app description, from the first bytes, before erasing anything. The project name in it must match the one the HMAC covers.
  - Project name `cyd_launcher`: it goes to the factory partition, which holds the launcher.
  - Anything else: it goes to the OTA slot that isn't running, which then becomes the boot slot.
  - It refuses an image that isn't a valid ESP32 app image or doesn't fit its target partition.
- **Writing the launcher.** ESP-IDF's OTA calls can't target the factory partition. `cyd_update` erases and writes it with the plain partition calls, only after the HMAC check, then checks the SHA-256.
- **Every image knows which commit it came from.** Each project's `CMakeLists.txt` sets the app description's version to the commit time and short hash of the commit it was built from, marked when the tree had uncommitted changes. The pause rule and the "failed release" rule both compare these commit times.
- **Records.** The board keeps, per image (launcher, slot A, slot B):
  - where it came from (GitHub, Mac or USB);
  - its commit;
  - its ELF SHA-256, from the image's app description.

  At start, an image whose ELF SHA-256 doesn't match its record was flashed over USB, so it starts a pause. The app also writes the per-slot records the launcher keeps in its `launcher` NVS namespace (`name<n>`, `ver<n>`), so the launcher's menu shows what's there:
  - a GitHub install records the asset's `updated_at`, as the launcher's own downloads do;
  - a Mac push records a version the launcher shows as `pushed over LAN`, not `flashed over USB`.

### Rollback

- **ESP-IDF's app rollback, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, goes in every project's `sdkconfig.defaults`, not just the launcher's.**
  - The bootloader is built from the launcher project, so it needs the option there.
  - Whichever image switches slots writes the new slot's state, and needs the option to write it as "new". That's both the launcher's menu and `cyd_update`.
- **The custom bootloader's non-power-on path** already defers to `bootloader_utility_get_selected_boot_partition`, which honours rollback states.
- **Apps mark themselves valid after their good start.**
  - `cyd_update` does this.
  - An app without `cyd_update` must call `esp_ota_mark_app_valid_cancel_rollback` itself, or it rolls back at its next restart. Say so in the docs for new apps.
- **During the good-start window,** ESP-IDF refuses to begin another update. The board answers a push with "busy" and the seconds left; the Mac script waits and retries. GitHub checks wait too.
- **The launcher's menu skips a slot that rolled back.** That covers its countdown default (`default_slot`) and the slot it boots when nothing is tapped. Such a slot's state reads as invalid or aborted.

### A bad launcher write

- The builder check confirmed that validation runs at power-on. When factory fails it, ESP-IDF's loader tries factory, then `ota_0`, then `ota_1`, ignoring which slot `otadata` selects.
- **Change the custom bootloader to try the selected slot first** when factory fails.
  - About 2.4 KB of room is left: the bootloader is 26,224 of 28,672 bytes.
  - If the change doesn't fit, keep the built-in order, which still reaches an app, just maybe the older one.

### CI

- **CI publishes the launcher too.**
  - The launcher's `firmware.bin` goes on a release tagged `launcher`, built from `main`.
  - The launcher's own image menu skips that release, so it never offers to put itself in a slot.
  - CI still checks app sizes against the slot size, and checks the launcher against the factory partition's 1,310,720 bytes.
- **CI publishes an image only when its inputs changed** *(designer's call)*.
  - Today every push to `main` re-uploads every image, which would make the board reinstall and restart for a README edit.
  - Each release records the commit it was built from, with its time.
  - CI republishes an image only when that commit and the new one differ under its folder (`launcher/` or `apps/<name>/`) or under `components/`. Otherwise it leaves the release alone.
  - If the recorded commit isn't in the history, it publishes.
- **The board decides from the release's text, before downloading.** That text carries the commit time and hash.

### The Mac side

- Each project's `platformio.ini` makes `pio run -t upload` run a script in `tools/` that computes the HMAC and sends the build.
  - That replaces today's `pio run -t upload` over USB. That path is a known trap: it writes a stock bootloader for the launcher, and a standalone layout for an app.
  - The board's address comes from `upload_port`, or from an environment variable, defaulting to the mDNS name *(builder's choice how)*.
- **Send the bytes with the system `curl`,** not PlatformIO's Python.
  - macOS blocks local-network connections from PlatformIO's Python itself (`No route to host`, seen on 2026-10-25).
  - A `curl` started from that same Python reaches the LAN (checked 2026-10-26 from a terminal).
  - From VS Code's upload button, macOS may ask once to let VS Code use the local network.
  - The HMAC is computed locally in the script; only the network transfer goes through `curl`.
- USB flashing stays documented with esptool, as today.

### The one-time USB step

Getting this feature onto the board takes one last USB flash: the new bootloader (rollback, selected-slot fallback), the new launcher (rollback option, skips the `launcher` release, `Update key` field) and the new app. Write them without erasing, so settings survive. Those images then count as flashed over USB, so GitHub leaves each one alone until CI publishes a release of it built from a later commit. After that, everything else can arrive over the network.

## Constraints

- No PSRAM, about 320 KB of DRAM. An update streams into flash in chunks; nothing buffers a whole image.
- The app must still fit a slot: `home_dashboard` is 1,187,968 bytes against 1,376,256, so the HTTP server, mDNS and the update code share about 188 KB. Report the size.
- The launcher must fit the factory partition (1,310,720 bytes; it is 1,097,472 today).
- No credentials in any binary or committed file. The update key, like the HA token, lives only in NVS and in the Mac's private file.
- GitHub's unauthenticated API allows 60 requests an hour per IP. An hourly check of two releases is well inside that. Fetch the two releases by tag, not the whole release list.
- Host-testable logic goes in pure C modules with tests in `test/`, as before. That covers:
  - deciding what to install, including the pause and failed-release rules;
  - parsing the version string and GitHub's reply;
  - checking the HMAC framing.

## Not in scope

- Updating the bootloader or partition table over the network.
- A web page on the board for uploading builds. The Mac command is the interface.
- Updating several boards at once.
- Encrypting the transfer. The HMAC proves who made the build, and the image itself isn't secret: it's public on GitHub anyway.
- Changing how the launcher's own menu downloads work, apart from skipping the `launcher` release and rolled-back slots.

## Would make it a failure even if it works

- Someone on the Wi-Fi can replace the firmware, or erase a slot or the launcher, without the key.
- A bad app build leaves the board stuck in a crash loop with no way back short of USB.
- The board installs a GitHub release over a test build the user just pushed, or over a USB fix.
- The board reinstalls a release that already failed its good start.
- Every push to `main` restarts the board, even when the image didn't change.
- An update interrupts the user mid-photo, or the board restarts for a launcher-only update.
- The key shows up in a log, a commit, a binary or a shell history.

## Left to the builder

- The HTTP API shape: paths, headers, the challenge's format, and error replies. Make the Mac script print the board's answer: installed, refused (and why), or busy.
- How the per-image records and pause state are stored in NVS.
- The exact version string, and where the commit goes in the release's text.
- The mDNS component version and how the address is configured on the Mac side.
- Whether `cyd_update` also serves a tiny `GET` showing the running image and version, for the Mac script to print.
- How the launcher shows `pushed over LAN` versions.
- Whether the selected-slot fallback fits the bootloader.
