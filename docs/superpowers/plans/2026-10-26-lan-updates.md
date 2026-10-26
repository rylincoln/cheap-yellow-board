# Updates Over the Network Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The board installs new launcher and app builds by itself, pushed from the Mac or published on GitHub, and goes back to the previous app build when a new one fails its first minute.

**Architecture:** A shared ESP-IDF component, `components/cyd_update`, runs inside the app. It has four parts:
- an HTTP push server with mDNS;
- an hourly GitHub check;
- an install engine that writes the launcher (factory partition) or the OTA slot that isn't running;
- the good-start and rollback bookkeeping.

The rules for what to install, the push authentication framing and image parsing are pure C modules with host tests. On the Mac, `tools/cyd_push.py` sends builds and is wired to `pio run -t upload`. CI publishes the launcher too, only republishes images whose inputs changed, and records each release's commit in its notes.

**Tech Stack:**
- ESP-IDF 6.1.0 C on PlatformIO espressif32@7.1.3: `esp_http_server`, `esp_http_client` with the certificate bundle, `app_update`, Mbed TLS 4.1 `mbedtls/md.h`, `espressif/mdns` 1.14.0.
- Python 3 standard library, with the system `curl`.
- GitHub Actions with `gh`.

**Spec:** `docs/superpowers/specs/2026-10-26-lan-updates-design.md`

## Global Constraints

- **Building.** Build each project from its folder with `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run`. A clean build is the check for device code; report `firmware.bin`'s size.
- **Host tests.** Run `sh test/run.sh`. C tests are C11 built with `cc -std=c11 -Wall -Wextra -Werror` and use `CHECK`, `CHECK_STR` and `CHECK_DONE` from `test/check.h`. Each new C test is two lines in `test/run.sh`, a build and a run.
- **Pure modules include only C standard headers,** so they build on the host.
- **Size limits:**
  - an app's `firmware.bin`: 1,376,256 bytes (a slot);
  - the launcher's: 1,310,720 bytes (factory);
  - the second-stage bootloader: 28,672 bytes, from 0x1000 to the partition table at 0x8000. It is 26,224 today.
- **Streaming.** There is no PSRAM and about 320 KB of DRAM. Images stream to flash in chunks of at most 4,096 bytes; nothing holds a whole image.
- **Secrets.** No key, token or password goes in a committed file, a binary, a log line or a command line. The board never logs the update key. The Mac script prints it only for `--show-key`, which the user runs themselves; an agent never runs `--show-key`.
- **Settings changes.** After changing a project's `sdkconfig.defaults`, delete its generated, git-ignored `sdkconfig.esp32dev` before building.
- **Style.** Match the surrounding code: `static` helpers, snake_case, and short `/* */` comments above functions that say why. User-facing text is plain English.
- **Commits.**
  - One commit per task, with a short imperative subject in the repo's style, such as "Skip rolled-back slots in the launcher".
  - Authored as the configured git user, with no AI attribution or co-author trailer.
- **Serial and esptool** commands run with the Bash sandbox disabled. The CH340 port can wedge until the board is replugged.
- **Pushing** to GitHub needs the user's explicit yes, each time.

## Shared Values

Every task that touches these uses them exactly.

**Push protocol** (HTTP on port 80; replies are one line of plain text):

1. **`GET /update/challenge`** replies 200 with 32 lowercase hex digits: 16 bytes from `esp_fill_random`.
   - Issuing one replaces any earlier challenge.
   - A challenge lives 60 s.
   - Any `POST /update` uses up the live challenge, whatever its outcome.
   - With no update key saved, it replies 403 `no update key on the board: set one on the phone setup page`.
2. **`POST /update`** carries the image as its body, plus these headers:

   | Header | Value |
   |---|---|
   | `Content-Length` | the image's length |
   | `X-Cyd-Challenge` | the challenge |
   | `X-Cyd-Project` | the image's project name |
   | `X-Cyd-Sha256` | 64 lowercase hex: the SHA-256 of the body |
   | `X-Cyd-Commit-Time` | decimal seconds: the committer time of the Mac's `HEAD` |
   | `X-Cyd-Hmac` | 64 lowercase hex: HMAC-SHA256 of the message below, keyed with the 20 key characters |

3. **The message** is exactly
   `"cyd-update-1\n" challenge "\n" length "\n" sha256 "\n" project "\n" commit_time "\n"`,
   with numbers in decimal.
4. **The order of checks:** key saved, then headers, then lock, then challenge, then HMAC, then the first 256 bytes (image description). Only then is anything erased.
5. **Replies:**

   | Status | Text |
   |---|---|
   | 200 | `installed <project> in slot <A\|B>; restarting` |
   | 200 | `installed the launcher; it runs at the next power-on` |
   | 400 | `missing or malformed X-Cyd headers` |
   | 400 | `not an app image` |
   | 400 | `project differs from the image` |
   | 400 | `too big for <partition label>` |
   | 403 | `no update key on the board: set one on the phone setup page` |
   | 403 | `wrong key or expired challenge` |
   | 409 | `busy <seconds>` |
   | 422 | `image hash differs` |
   | 422 | `image does not verify` |
   | 500 | `flash error <esp_err_to_name>` |

**mDNS host name:** `cyd-` plus the last four hex digits of the Wi-Fi station MAC address, in lowercase. The user's board is `cyd-e5d0`.

**Update key:**
- 20 characters from `0123456789ABCDEFGHJKMNPQRSTVWXYZ`, about 100 bits.
- The Mac writes it as `XXXX-XXXX-XXXX-XXXX-XXXX`.
- To normalize a key: drop spaces and dashes, uppercase it, read `O` as `0` and `I` or `L` as `1`.
- It is stored normalized in NVS `cyd_config` key `update_key`. The Mac file is `~/.config/cyd/update_key`, mode 600, or the file `CYD_KEY_FILE` names.

**Image version.** The app description's version is `<commit time>-<7-hex short hash>`, with `+` appended when tracked files differ from `HEAD`. It is `0-unknown` without git.

**Release notes,** written by CI:

```
Boot image for <launcher|apps/NAME>, built from <7-hex>.

commit: <40-hex sha>
commit-time: <seconds>
```

**Launcher slot records** (NVS namespace `launcher`, keys `name<n>` and `ver<n>`, n = 0 or 1 for `ota_0` or `ota_1`):
- A Mac push writes `ver<n>` = `pushed over LAN`.
- A GitHub install writes the asset's `updated_at`.

**Floors.** There is one floor per image: the launcher, and the running app's project.
- A GitHub release installs only when its notes' commit time is above 0 and above the floor.
- After a GitHub install, the floor is the release's commit time. A release that then fails its good start therefore isn't tried again.
- After a Mac push, the floor is the larger of `X-Cyd-Commit-Time` and the last release commit time seen for that image.
- After a USB flash, detected by an unknown ELF SHA-256, the floor is the larger of the image version's commit time and the last release commit time seen.
- Floors never gate Mac pushes.

**Timings:**

| Event | Time |
|---|---|
| Good start | 60 s of uptime, plus the first screen drawn |
| Challenge lifetime | 60 s |
| GitHub check | when the network comes up, then every 60 min |
| `RESTARTING` | shown 1 s |
| `LAUNCHER UPDATED` | shown 3 s |
| `UPDATE FAILED` | shown 3 s |
| A Mac push's restart | waits at most 10 s for the screen |

**Screen:**
- While updating:
  - row 0 `UPDATING` in cyan;
  - row 2 the label (`dashboard` or `launcher`) in white;
  - row 3 `from Mac` or `from GitHub` in gray;
  - row 4 `<n>%` in white.
- Closing screens:
  - `RESTARTING` in cyan;
  - `LAUNCHER UPDATED` in cyan, with `runs at the next` and `power-on` on rows 2 and 3;
  - `UPDATE FAILED` in yellow, with a reason on row 2.

**GitHub request:**
- `GET https://api.github.com/repos/<CYD_IMAGE_REPO>/releases/tags/<tag>`.
- `Accept: application/vnd.github+json`, with the certificate bundle.
- The reply is at most 16 KB.
- `CYD_IMAGE_REPO` is `"rylincoln/cheap-yellow-board"`.

## Review Focus

These are the most likely ways for this to bite, most likely first. Each is pinned by the test or check named.

1. **A push cut off part-way** (a Wi-Fi drop, or Ctrl-C on the Mac). The current build keeps running, the half-written slot drops out of the launcher's menu, the lock and the screen come back, and the next push installs. Pinned by the Task 12 check "cut-off push".
2. **Two updates at once:** a second Mac push, or a push during a GitHub install. The second gets `busy <seconds>`, and nothing is corrupted. Pinned by the Task 12 check "busy".
3. **A Mac push while the dashboard is mid-photo or mid-fetch.** Nothing draws over anything else, and the board restarts within 10 s of the transfer ending. Pinned by the Task 12 check "push mid-photo".
4. **A release caught mid-publish:** `firmware.bin` missing, or notes without `commit-time`. Nothing installs and the floor doesn't change. Pinned by Task 3's tests `release without firmware.bin` and `notes without commit-time`.
5. **The wrong file pushed:** `bootloader.bin`, `partitions.bin`, or a truncated image. The script refuses it, and so does the board, before anything is erased. Pinned by Task 2's `image_desc` rejection tests and Task 4's `read_project` tests.

## File Map

| Path | Responsibility | Task |
|---|---|---|
| `components/json_scan/` | The JSON scanner, moved from the dashboard and shared | 1 |
| `components/cyd_update/push_auth.{c,h}` | Challenge bookkeeping, the HMAC message, hex parsing and constant-time compare (pure) | 2 |
| `components/cyd_update/image_desc.{c,h}` | Reading an image's app description from its first 256 bytes (pure) | 2 |
| `components/cyd_update/update_rules.{c,h}` | Release parsing, version and notes commit times, floors (pure) | 3 |
| `tools/cyd_push.py`, `tools/test_cyd_push.py` | The Mac side: the key, the push and its tests | 4 |
| `tools/cyd_version.cmake` | Stamps the image version from git | 5 |
| `.github/workflows/build.yml` | Publishes the launcher, only changed images, and commit notes | 5 |
| `launcher/main/{form_check,phone_setup,main}.c` | The update key field, skipping the `launcher` release and rolled-back slots | 6 |
| `launcher/bootloader_components/main/bootloader_start.c` | Starting the selected app when the launcher image is bad | 7 |
| `components/cyd_update/{cyd_update,records,install}.c` | The public API, good start, records, floors, screen lending, install engine | 8 |
| `components/cyd_update/push_server.c` | `/update/challenge`, `/update` and mDNS | 9 |
| `components/cyd_update/github_check.c` | The hourly release check, download and install | 10 |
| `docs/INSTALL.md`, READMEs, `CLAUDE.md` | How to set the key, push, publish and recover | 11 |

---

### Task 1: Share the JSON scanner

**Files:**
- Move (git mv): `apps/home_dashboard/main/json_scan.c` to `components/json_scan/json_scan.c`
- Move (git mv): `apps/home_dashboard/main/json_scan.h` to `components/json_scan/include/json_scan.h`
- Create: `components/json_scan/CMakeLists.txt`
- Modify: `apps/home_dashboard/main/CMakeLists.txt`, `test/run.sh`

**Interfaces:**
- Produces: the ESP-IDF component `json_scan`, with its header `json_scan.h` unchanged.

- [ ] **Step 1:** `git mv` both files. Create `components/json_scan/CMakeLists.txt` with `idf_component_register(SRCS "json_scan.c" INCLUDE_DIRS "include")`.
- [ ] **Step 2:** In `apps/home_dashboard/main/CMakeLists.txt`, drop `"json_scan.c"` from SRCS and add `json_scan` to PRIV_REQUIRES.
- [ ] **Step 3:** In `test/run.sh`, change the wildlife build line to `-Iapps/home_dashboard/main -Icomponents/json_scan/include` and `components/json_scan/json_scan.c`.
- [ ] **Step 4:** Run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 5:** Build `apps/home_dashboard` and `launcher`. Expected: both succeed, and the dashboard is within 1 KB of 1,187,968.
- [ ] **Step 6:** Commit: "Share the JSON scanner as a component".

### Task 2: Push authentication and image checks (pure C)

**Files:**
- Create: `components/cyd_update/push_auth.h`, `push_auth.c`, `image_desc.h`, `image_desc.c`, and `CMakeLists.txt` with `idf_component_register(SRCS "push_auth.c" "image_desc.c" PRIV_INCLUDE_DIRS ".")`
- Test: `test/test_push_auth.c`, `test/test_image_desc.c`
- Modify: `test/run.sh`, adding `-Icomponents/cyd_update` builds of each test with its module

**Interfaces:**
- Produces, in `push_auth.h`:
  ```c
  #define PUSH_CHALLENGE_HEX_LENGTH 32
  #define PUSH_CHALLENGE_LIFETIME_US (60LL * 1000 * 1000)
  typedef struct {
      char hex[PUSH_CHALLENGE_HEX_LENGTH + 1];
      int64_t issued_us;
      bool live;
  } push_challenge_t;
  void push_challenge_issue(push_challenge_t *challenge, const uint8_t random[16], int64_t now_us);
  /* True once: hex matches, it is under the lifetime old, and unused. Any call uses it up. */
  bool push_challenge_take(push_challenge_t *challenge, const char *hex, int64_t now_us);
  /* The text the HMAC covers; returns its length, or 0 when it doesn't fit or a field holds '\n'. */
  size_t push_auth_message(char *output, size_t capacity, const char *challenge, uint32_t length,
                           const char *sha256_hex, const char *project, int64_t commit_time);
  bool push_auth_parse_hex32(const char *hex, uint8_t output[32]); /* exactly 64 hex digits, either case */
  bool push_auth_equal(const uint8_t a[32], const uint8_t b[32]);  /* constant time */
  ```
- Produces, in `image_desc.h`:
  ```c
  #define IMAGE_DESC_BYTES 256
  typedef struct {
      char project[32];
      char version[32];
      uint8_t elf_sha256[32];
  } image_desc_t;
  /* Byte 0 is 0xE9; 0xABCD5432 (little-endian) at 32; version at 48; project at 80, NUL-terminated
     and not empty; ELF SHA-256 at 176. */
  bool image_desc_read(const uint8_t *image, size_t length, image_desc_t *desc);
  ```

- [ ] **Step 1: Write `test/test_push_auth.c`:**
  - **message:** `push_auth_message(out, 512, "00112233445566778899aabbccddeeff", 1187968, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "home_dashboard", 1761488715)` gives `"cyd-update-1\n00112233445566778899aabbccddeeff\n1187968\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\nhome_dashboard\n1761488715\n"`, and returns its `strlen`.
  - **Too small:** capacity 20 returns 0.
  - **Newline:** project `"a\nb"` returns 0.
  - **Hex:** `push_auth_parse_hex32` of that SHA string gives true, with byte 0 = 0xe3 and byte 31 = 0x55. The uppercase form is true. 63 or 65 digits are false, and so is a `g` anywhere.
  - **Compare:** `push_auth_equal` is true for equal arrays and false when only the last byte differs.
  - **Challenge:**
    - Issue with bytes 0x00 to 0x0f at 0. `hex` is `"000102030405060708090a0b0c0d0e0f"`.
    - Take with it at 59,999,999: true. Again: false.
    - Re-issue, then take with a wrong hex: false. Then the right hex: false.
    - Re-issue at 0, then take at 60,000,001: false.
- [ ] **Step 2: Write `test/test_image_desc.c`** with a 256-byte buffer:
  - byte 0 = 0xE9;
  - bytes 32 to 35 = `32 54 CD AB`;
  - `"1793027864-1dec0ea"` at 48;
  - `"home_dashboard"` at 80;
  - bytes 176+i = i for i in 0 to 31.

  Checks:
  - it reads all three fields;
  - length 255 is false;
  - byte 0 = 0x00 is false;
  - bytes 0 to 1 = `AA 50` is false, like a partition table;
  - 0xE9 with zeros at 32 is false, like `bootloader.bin`;
  - a project of 32 non-NUL bytes is false;
  - an empty project is false.
- [ ] **Step 3:** Add both to `test/run.sh`, run it, and see both fail to link or fail their checks.
- [ ] **Step 4:** Implement both modules.
- [ ] **Step 5:** Run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 6:** Build `apps/home_dashboard`. The new component compiles into it; expect success.
- [ ] **Step 7:** Commit: "Check pushed images and their signatures".

### Task 3: Update rules and GitHub release parsing (pure C)

**Files:**
- Create: `components/cyd_update/update_rules.h`, `update_rules.c`
- Modify: `components/cyd_update/CMakeLists.txt` (SRCS adds `update_rules.c`; PRIV_REQUIRES `json_scan`), `test/run.sh` (links `components/json_scan/json_scan.c`)
- Test: `test/test_update_rules.c`

**Interfaces:**
- Consumes: `json_scan.h` (Task 1).
- Produces:
  ```c
  typedef struct {
      char url[256];        /* firmware.bin's browser_download_url */
      uint32_t size;
      char sha256[65];      /* hex from the asset's "digest": "sha256:<hex>", else empty */
      char updated_at[24];  /* the asset's updated_at */
      int64_t commit_time;  /* the notes' "commit-time:" value, else 0 */
  } update_release_t;
  /* From GitHub's reply to GET .../releases/tags/<tag>; false when there is no firmware.bin asset. */
  bool update_parse_release(const char *json, size_t length, update_release_t *release);
  int64_t update_notes_commit_time(const char *notes);    /* digits after "commit-time: ", else 0 */
  int64_t update_version_commit_time(const char *version); /* leading digits before '-', else 0 */
  bool update_release_wanted(int64_t release_time, int64_t floor);
  int64_t update_floor_after_local(int64_t image_time, int64_t last_seen_release_time);
  ```
  Read the notes into a buffer of at least 512 bytes. `json_scan` decodes `\n` to `?`, so the notes parser must not depend on line breaks.

- [ ] **Step 1: Write `test/test_update_rules.c`.**

  `update_notes_commit_time`:
  - The Shared Values notes with commit-time `1793027864` give 1793027864.
  - The same text with each `\n` replaced by `?` gives the same.
  - `"Boot image for apps/home_dashboard, built from 4432784."` gives 0.
  - `"commit-time: x"` gives 0.

  `update_version_commit_time`:
  - `"1793027864-1dec0ea"` and `"1793027864-1dec0ea+"` give 1793027864.
  - `"bluetooth_diag-105-g4432784"`, `"0-unknown"`, `""` and `"1793027864"` give 0.

  `update_release_wanted`:
  - `(1793027865, 1793027864)` is true.
  - `(1793027864, 1793027864)` is false.
  - `(0, 0)` is false.
  - `(5, 0)` is true.

  `update_floor_after_local`: `(100, 200)` gives 200, `(300, 200)` gives 300, and `(0, 0)` gives 0.

  `update_parse_release`, on a JSON literal shaped like GitHub's reply:
  - It has `url`, a nested `author` object, `tag_name` `home_dashboard`, `body` (the Shared Values notes with JSON `\n` escapes), and `assets`.
  - The first asset is `notes.txt`, with its own `digest` and `updated_at`.
  - The second is `firmware.bin`, with size 1187984, digest `sha256:50d50ebfbb1481ef5c8805b3432e1fa990cc35d1c7e67d9c008a38f9ac455800`, `updated_at` `2026-10-26T14:25:17Z`, url `https://github.com/rylincoln/cheap-yellow-board/releases/download/home_dashboard/firmware.bin`, and a nested `uploader` object.

  Cases:
  - **`parsed release`:** all five fields come out exactly.
  - **`release without firmware.bin`:** the asset removed gives false.
  - **`no digest`:** `firmware.bin` without `digest` gives true, with `sha256` `""`.
  - **`sha1 digest`:** digest `"sha1:ab"` gives `sha256` `""`.
  - **`notes without commit-time`:** gives true with `commit_time` 0, and `update_release_wanted(0, 0)` is false.
- [ ] **Step 2:** Add it to `test/run.sh` and run. Expected: FAIL.
- [ ] **Step 3:** Implement `update_rules.c`.
- [ ] **Step 4:** Run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 5:** Commit: "Decide which GitHub releases to install".

### Task 4: The Mac push script

**Files:**
- Create: `tools/cyd_push.py` and `tools/test_cyd_push.py`, using only the standard library. They must run on Python 3.9 to 3.13: PlatformIO's Python, and the system `python3`.
- Modify: `test/run.sh` (add `python3 tools/test_cyd_push.py` before the final echo), `launcher/platformio.ini`, `apps/home_dashboard/platformio.ini`

**Interfaces:**
- Produces, in `cyd_push.py`:
  - `ALPHABET`;
  - `normalize_key(text: str) -> str`, which raises `ValueError`;
  - `new_key() -> str`, which returns the `XXXX-XXXX-XXXX-XXXX-XXXX` form using `secrets`;
  - `auth_message(challenge: str, length: int, sha256_hex: str, project: str, commit_time: int) -> bytes`;
  - `read_project(image: bytes) -> str`, with the same rules as `image_desc_read`; it raises `ValueError`;
  - `board_host(environ, host_file: Path) -> str`;
  - `busy_seconds(reply: str) -> Optional[int]`;
  - `main(argv) -> int`.
- **Test hook:** `CYD_CURL_EXTRA`, split with `shlex`, is appended to the push's `curl` arguments, such as `--max-time 4` to cut a push off. It never carries the key.
- **CLI:**
  - `cyd_push.py FIRMWARE.bin` pushes.
  - `cyd_push.py --new-key [--force]` writes the key file and copies the key with `pbcopy`. It prints only where the key went.
  - `cyd_push.py --show-key` prints it.
- **platformio.ini,** under `[env:esp32dev]`, with a one-line comment:

  ```
  upload_protocol = custom
  upload_command = "$PYTHONEXE" "$PROJECT_DIR/../tools/cyd_push.py" "$SOURCE"
  ```

  For the app, use `../../tools`.

- [ ] **Step 1: Write `tools/test_cyd_push.py`** (unittest; it puts its own folder on `sys.path`).

  `normalize_key`:
  - `"abcd-1234-efgh-5678-jkmn"` gives `"ABCD1234EFGH5678JKMN"`.
  - `" ABCD 1234 EFGH 5678 JKMO\n"` gives `"ABCD1234EFGH5678JKM0"`.
  - `"ilo0-1234-efgh-5678-jkmn"` gives `"11001234EFGH5678JKMN"`.
  - `"ABCD-1234"` raises, and so does `"ABCD-1234-EFGH-5678-JKMU"`.

  `new_key`:
  - It matches `^[0-9A-HJKMNP-TV-Z]{4}(-[0-9A-HJKMNP-TV-Z]{4}){4}$`.
  - It normalizes to 20 characters.
  - Two calls differ.

  `auth_message`: `auth_message("00112233445566778899aabbccddeeff", 1187968, "e3b0…b855", "home_dashboard", 1761488715)` equals Task 2's message as bytes. The HMAC-SHA256 under key `"ABCD1234EFGH5678JKMN"` is `cb329fbcc91a59decaaa1d633be21bfea84b329c008c30f8edd4f1ae9d660e80`.

  `read_project`: Task 2's 256-byte buffer gives `"home_dashboard"`. The `bootloader.bin`-shaped buffer raises, and so does a 100-byte buffer.

  `board_host`:
  - `{"CYD_HOST": "cyd-e5d0.local"}` with a missing file gives `"cyd-e5d0.local"`.
  - `{}` with a file holding `"192.168.1.126\n"` gives `"192.168.1.126"`.
  - Neither raises a `RuntimeError` whose message names `CYD_HOST`.

  `busy_seconds`: `"busy 42"` gives 42, and `"installed the launcher; it runs at the next power-on"` gives None.
- [ ] **Step 2:** Run `python3 tools/test_cyd_push.py`. Expected: FAIL, with an import error.
- [ ] **Step 3: Implement `cyd_push.py`.**
  - **The key file** is checked before anything else: `CYD_KEY_FILE`, else `~/.config/cyd/update_key`. Refuse when it is missing (say to run `--new-key`) or readable by group or others (say to `chmod 600`). Write it with `os.open(path, O_WRONLY|O_CREAT|O_EXCL, 0o600)`, unless `--force`.
  - **The host** is `CYD_HOST`, else the first line of `~/.config/cyd/host`.
  - **The commit time** is `git -C <repo root, the script's parent's parent> log -1 --format=%ct`.
  - **All network traffic goes through `curl` on PATH**, because macOS blocks PlatformIO's Python on the LAN:
    - the challenge: `curl -sS --max-time 10`;
    - the push: `curl -sS --max-time 180 -H 'Expect:'`, then each `X-Cyd-*` header, `--data-binary @FIRMWARE` and `-w '\n%{http_code}'`.
  - **On `busy N`,** wait N+1 s and retry with a new challenge, at most 3 times.
  - **Output and exit code:** print `<host>: <reply>`. Exit 0 only on HTTP 200.
  - **The HMAC is computed in Python** with `hmac` and `hashlib`.
- [ ] **Step 4:** Run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 5:** In each project folder, run `USERPROFILE="$HOME" ~/.platformio/penv/bin/pio run -t upload` with `CYD_HOST` unset, no host file and `CYD_KEY_FILE` pointing at a missing scratch path. Expected: it fails with a one-line message saying how to make a key, with no traceback. This proves the command reaches the script without touching a board.
- [ ] **Step 6:** Commit: "Push builds to the board from the Mac".

### Task 5: Releases record their commit; CI publishes the launcher and only changed images

**Files:**
- Create: `tools/cyd_version.cmake`
- Modify: `launcher/CMakeLists.txt`, `apps/home_dashboard/CMakeLists.txt` (each includes the file after `project.cmake` and before `project()`), `.github/workflows/build.yml`

**Interfaces:**
- Produces:
  - the Image version and Release notes formats in Shared Values;
  - releases tagged `launcher` and `<app folder>`.

- [ ] **Step 1: Write `tools/cyd_version.cmake`.** It sets `PROJECT_VER` from `git log -1 --format=%ct-%h --abbrev=7`, adds `+` when `git diff --quiet HEAD` exits 1, and falls back to `0-unknown`. Its comment says that PlatformIO re-runs CMake only when `CMakeLists.txt` or `sdkconfig` change, so a local build's stamp can lag behind `HEAD`. Mac pushes therefore send their own commit time.
- [ ] **Step 2:** Include it from both projects. Editing each `CMakeLists.txt` makes PlatformIO reconfigure, so the stamp applies on the next build.
- [ ] **Step 3:** Build `apps/home_dashboard`. Read 32 bytes at offset 48 of `firmware.bin`. Expected: `<ct>-<hash>` matching `git log -1 --format=%ct-%h --abbrev=7`, with `+` since the tree is dirty, and under 32 characters.
- [ ] **Step 4: Change `build.yml`.**
  - **Checkout.** The build job's checkout gets `fetch-depth: 0`.
  - **Size check.** One step checks every project: the launcher against 1310720, and apps against 1376256.
  - **Publishing.** Publish runs for every project on `main`, the launcher included. `DIR` is `launcher` or `apps/<name>`.
  - **Skipping unchanged images.** Read the previous `commit:` from `gh release view "$TAG" --json body --jq .body`. If that commit exists (`git cat-file -e`) and `git diff --quiet "$prev" HEAD -- "$DIR" components tools/cyd_version.cmake` holds, print that the image is unchanged and stop.
  - **Order.** For an existing release, upload `firmware.bin` with `--clobber`, then `gh release edit --notes`. For a new one, `gh release create "$TAG" firmware.bin --target "$GITHUB_SHA" --title "$TAG" --notes "$notes"`. Notes go after the asset, so the board never pairs new notes with an old asset.
  - **Notes** come from `printf`, in exactly the Shared Values format, with `git log -1 --format=%ct`.
  - **The header comment** describes all of this.
- [ ] **Step 5:** Dry-run the skip test locally. With `prev=$(git rev-parse HEAD~1)`, `git diff --quiet "$prev" HEAD -- launcher components tools/cyd_version.cmake; echo $?` prints 0 or 1 without an error. Then check the YAML parses with `ruby -ryaml -e 'YAML.load_file(".github/workflows/build.yml")'`.
- [ ] **Step 6:** Commit: "Publish the launcher and only images that changed".

### Task 6: The launcher's update key, release list and rolled-back slots

**Files:**
- Modify:
  - `launcher/main/form_check.{h,c}` and `test/test_form_check.c`;
  - `launcher/main/phone_setup.{h,c}` and `launcher/main/main.c`;
  - `launcher/sdkconfig.defaults`;
  - `components/cyd_display/include/cyd_display.h`.

**Interfaces:**
- Produces:
  - `bool form_update_key_normalize(char *key);` normalizes in place. It is true for an empty result (blank keeps the saved key) or a valid 20-character key.
  - In `cyd_display.h`, next to `CYD_CONFIG_NAMESPACE`:
    - `#define CYD_CONFIG_UPDATE_KEY "update_key"`;
    - `#define CYD_IMAGE_REPO "rylincoln/cheap-yellow-board"`.
  - The launcher's `IMAGE_REPO` default becomes `CYD_IMAGE_REPO`.

- [ ] **Step 1: Add to `test/test_form_check.c`** (buffers of 40):
  - `"abcd-1234-efgh-5678-jkmn"` gives true and `"ABCD1234EFGH5678JKMN"`;
  - `" ABCD 1234 EFGH 5678 JKMO\n"` gives true and `"ABCD1234EFGH5678JKM0"`;
  - `"ilo0-1234-efgh-5678-jkmn"` gives true and `"11001234EFGH5678JKMN"`;
  - `"  "` gives true and `""`;
  - `"ABCD-1234"`, `"ABCD-1234-EFGH-5678-JKMU"` and `"ABCD-1234-EFGH-5678-JKMN-P"` give false.
- [ ] **Step 2:** Run `sh test/run.sh`. Expected: FAIL.
- [ ] **Step 3:** Implement `form_update_key_normalize`, then run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 4: Phone page.**
  - `phone_settings_t` gains `char update_key[40]`, and `phone_setup_config_t` gains `bool update_key_saved`.
  - Add an `Updates` section after the Home Assistant one: label `Update key`, then `<input name="update_key" type="text" maxlength="39" autocomplete="off" autocapitalize="characters" autocorrect="off" spellcheck="false">`.
  - Under the field, `<small>Blank keeps the saved key.</small>` when one is saved, else `<small>Make one on the Mac: tools/cyd_push.py --new-key</small>`.
  - Validate it with `form_update_key_normalize`. The error is `The update key is 20 letters and digits, as the Mac made it.`
  - Never send the key back to the page. Wipe it with `memset` wherever the token is wiped.
- [ ] **Step 5: `main.c`.**
  - `save_phone_settings` stores the normalized key under `CYD_CONFIG_UPDATE_KEY` when it isn't blank.
  - `phone_setup_start`'s config sets `update_key_saved`.
  - `fetch_release_images` skips tag `launcher` (`#define LAUNCHER_RELEASE_TAG "launcher"`).
  - `default_slot`'s fallback loop skips slots for which `esp_ota_get_state_partition` reports `ESP_OTA_IMG_INVALID` or `ESP_OTA_IMG_ABORTED`, through a helper `static bool slot_rolled_back(int slot)`.
- [ ] **Step 6:** In `launcher/sdkconfig.defaults`, add `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` with a one-line comment. Delete `launcher/sdkconfig.esp32dev`.
- [ ] **Step 7:** Build the launcher. Report the sizes of `firmware.bin` (at most 1,310,720) and `bootloader.bin` (at most 28,672).
- [ ] **Step 8:** Commit: "Take an update key and skip rolled-back slots in the launcher".

### Task 7: Start the selected app when the launcher image is bad

**Files:**
- Modify: `launcher/bootloader_components/main/bootloader_start.c`

**Interfaces:**
- Consumes: rollback enabled in the launcher's config (Task 6).

- [ ] **Step 1:** In the power-on branch, verify factory first: `esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &bs.factory, &metadata)` from `esp_image_format.h`.
  - If that fails, log `Launcher image is invalid: starting the selected app` and take `bootloader_utility_get_selected_boot_partition(&bs)`, resetting on `INVALID_INDEX` as the other branch does.
  - Update the file's header comment.
- [ ] **Step 2:** Build the launcher. Expected: `bootloader.bin` is at most 28,672 bytes, and IDF's own size check passes.
  - If it doesn't fit, add `CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y` to `launcher/sdkconfig.defaults`, delete `sdkconfig.esp32dev`, and rebuild.
  - If it still doesn't fit, revert this task's code and report it. The spec accepts the built-in fallback order.
- [ ] **Step 3:** Commit: "Start the selected app when the launcher image is bad".

### Task 8: `cyd_update` core: good start, records, screen lending, install engine

**Files:**
- Create in `components/cyd_update/`:
  - `include/cyd_update.h`;
  - `cyd_update.c`;
  - `update_internal.h`;
  - `records.h`, `records.c`;
  - `install.h`, `install.c`.
- Modify:
  - `components/cyd_update/CMakeLists.txt`: `INCLUDE_DIRS "include"`. PRIV_REQUIRES `app_update bootloader_support cyd_display esp_partition esp_system esp_timer json_scan mbedtls nvs_flash`.
  - `apps/home_dashboard/main/main.c` and `main/CMakeLists.txt`: PRIV_REQUIRES `cyd_update`.
  - `apps/home_dashboard/sdkconfig.defaults`: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. Delete `apps/home_dashboard/sdkconfig.esp32dev`.

**Interfaces:**
- Consumes: `image_desc.h` (Task 2), `update_rules.h` (Task 3), `CYD_CONFIG_UPDATE_KEY` (Task 6).
- Produces, in `cyd_update.h`, the public API:
  ```c
  typedef struct {
      const char *label;        /* on the UPDATING screen, such as "dashboard" */
      TaskHandle_t screen_task; /* the task that draws the app's screens */
      uint32_t screen_bit;      /* notified on screen_task when an update wants the screen */
  } cyd_update_config_t;
  esp_err_t cyd_update_init(const cyd_update_config_t *config); /* at start-up, before Wi-Fi */
  void cyd_update_screen_drawn(void);                /* the app's first screen is up */
  void cyd_update_network_up(void);                  /* Wi-Fi has an address; non-blocking, repeatable */
  void cyd_update_set_interruptible(bool interruptible); /* true on the status or readings screen */
  void cyd_update_lend_screen(void); /* on screen_task after screen_bit; returns when the update gives it back */
  ```
- Produces, in `update_internal.h`, for Tasks 9 and 10:
  ```c
  bool update_lock(int *busy_seconds);  /* false during the good-start window (60 s minus uptime, at least 1) or another update (30) */
  void update_unlock(void);
  bool update_screen_take(uint32_t wait_ms);  /* asks screen_task for the screen; true once lent */
  void update_screen_progress(const char *label, const char *source, int percent); /* source "Mac" or "GitHub"; draws only while lent */
  void update_screen_finish(const char *title, const char *detail, uint32_t show_ms, bool restarting);
  const char *update_app_label(void);
  bool update_app_interruptible(void);
  ```
- Produces, in `records.h`:
  ```c
  typedef enum { IMAGE_LAUNCHER, IMAGE_APP } image_kind_t;
  typedef enum { SOURCE_USB = 1, SOURCE_MAC = 2, SOURCE_GITHUB = 3 } image_source_t;
  int64_t records_floor(image_kind_t kind);
  int64_t records_seen(image_kind_t kind);
  void records_set_seen(image_kind_t kind, int64_t release_time);
  /* Notes an install: ELF SHA-256, source and commit time; the floor per Shared Values; for an OTA
     slot, the launcher's name<n>, and ver<n> = launcher_version. */
  void records_note_install(const esp_partition_t *partition, const image_desc_t *desc,
                            image_source_t source, int64_t commit_time, const char *launcher_version);
  void records_clear_slot(const esp_partition_t *partition); /* before erasing: the launcher's record too */
  void records_check_usb(void); /* running app and factory: an unknown ELF SHA-256 counts as a USB flash */
  ```
  The NVS layout is the builder's choice, in a new namespace `cyd_update`. The app's floor and seen time belong to the running project's name; reset both when the name changes.
- Produces, in `install.h`:
  ```c
  typedef enum { INSTALL_FROM_MAC, INSTALL_FROM_GITHUB } install_source_t;
  typedef struct install { /* fields are the builder's */ } install_t; /* callers keep one on their stack */
  /* project cyd_launcher: the factory partition, with plain erase and write; else the OTA slot that
     isn't running, through esp_ota_begin. ESP_ERR_INVALID_SIZE when it doesn't fit, before touching
     flash. Clears that slot's records, then erases. */
  esp_err_t install_begin(install_t *install, const image_desc_t *desc, size_t length, install_source_t source);
  esp_err_t install_write(install_t *install, const void *data, size_t length); /* also hashes */
  /* expected may be NULL (a GitHub asset without a digest). It checks the hash, verifies the image
     (esp_ota_end, or esp_image_verify for factory), records it, and makes an app the boot slot. */
  esp_err_t install_finish(install_t *install, const uint8_t *expected_sha256, int64_t commit_time,
                           const char *launcher_version);
  void install_abort(install_t *install);
  bool install_is_launcher(const install_t *install);
  int install_percent(const install_t *install);
  ```
  - Streaming SHA-256 uses `mbedtls_md_setup` with `mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)`, then `mbedtls_md_starts`, `update` and `finish`. IDF initializes PSA at start-up.
  - `install_finish` returns `ESP_ERR_INVALID_CRC` for a hash mismatch and `ESP_ERR_IMAGE_INVALID` for a failed verify. Task 9 maps these to its 422 replies.

- [ ] **Step 1: Implement the component.**
  - **`cyd_update_init`** does three things:
    - It logs the running partition and its OTA state, and logs `rolled back from slot X` when `esp_ota_get_last_invalid_partition()` is set.
    - It calls `records_check_usb()`.
    - It starts the good-start check. Once uptime reaches 60 s and `cyd_update_screen_drawn` has been called, a `PENDING_VERIFY` build calls `esp_ota_mark_app_valid_cancel_rollback()` and logs `Build marked good`.
  - **`cyd_update_network_up`** only records that the network is up in this task. Tasks 9 and 10 hook into it.
  - **Screens** use `cyd_text_row` with the Shared Values copy.
- [ ] **Step 2: Wire up the dashboard.**
  - Add `#define UPDATE_BIT BIT2`.
  - Call `cyd_update_init` in `app_main` after the dashboard task exists and before `wifi_init`, with label `"dashboard"`.
  - Call `cyd_update_network_up()` on `IP_EVENT_STA_GOT_IP`.
  - Call `cyd_update_screen_drawn()` after the first `update_status_screen()` in `dashboard_task`, before its loop.
  - Call `cyd_update_set_interruptible(true)` in `enter_glance`, and `(false)` in `enter_timed_screen`.
  - On `UPDATE_BIT`, call `cyd_update_lend_screen()`, then redraw in full: the status screen with `draw_status()`, the readings with `draw_glance(true)`, and any timed screen goes back to `enter_glance()`. Then clear `TAP_BIT`.
- [ ] **Step 3:** Run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 4:** Build the dashboard and the launcher. Report the dashboard's size, which must be at most 1,376,256.
- [ ] **Step 5:** Commit: "Keep a new build only after a good start".

### Task 9: Pushes from the Mac: the server and mDNS

**Files:**
- Create in `components/cyd_update/`: `push_server.h`, `push_server.c`, and `idf_component.yml` with `espressif/mdns: "==1.14.0"`.
- Modify:
  - `components/cyd_update/CMakeLists.txt`: PRIV_REQUIRES adds `esp_http_server esp_netif esp_wifi`.
  - `cyd_update.c`: the first `cyd_update_network_up()` starts the server.
  - `apps/home_dashboard/sdkconfig.defaults`: `CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048`, with the launcher's comment. Delete its `sdkconfig.esp32dev`.
  - Commit the generated `apps/home_dashboard/dependencies.lock` and the updated `launcher/dependencies.lock`.

**Interfaces:**
- Consumes: `push_auth.h`, `image_desc.h`, `update_internal.h`, `install.h`, `records.h` (Tasks 2 and 8). The protocol and host name are in Shared Values.
- Produces: `esp_err_t push_server_start(void);`, which is idempotent.

- [ ] **Step 1: Implement `push_server.c`.**
  - **The server:** `httpd_start` on port 80, stack 8192, `lru_purge_enable = true`.
  - **The challenge handler** keeps one `push_challenge_t`, using `esp_fill_random` and `esp_timer_get_time`.
  - **The update handler:**
    1. It reads the key from NVS on every request.
    2. It runs the checks in the Shared Values order, using `mbedtls_md_hmac` and `push_auth_equal`.
    3. It reads until 256 bytes or the end, then calls `image_desc_read` and compares the project.
    4. It requests the screen without waiting, by taking it with a wait of 0 and polling again during the transfer.
    5. It streams in 4,096-byte chunks through `install_*`, calling `update_screen_progress(label, "Mac", percent)` while the screen is lent.
    6. It calls `install_finish` with the header hash, commit time and `pushed over LAN`.
    7. It sends the reply. For an app, it then calls `update_screen_take(10000)`, shows `RESTARTING` for 1 s and calls `esp_restart()`. For the launcher, it shows `LAUNCHER UPDATED` for 3 s and gives the screen back.
  - **Every failure after `update_lock`** calls `install_abort` if needed, shows `UPDATE FAILED` if the screen is lent, and unlocks.
  - **mDNS:** `mdns_init()`, `mdns_hostname_set("cyd-xxxx")`, and `mdns_instance_name_set("CYD " label)`.
- [ ] **Step 2:** Build the dashboard and the launcher. Expected: both succeed. Report the dashboard's size; the limit is 1,376,256.
- [ ] **Step 3:** Commit: "Install builds pushed from the Mac".

### Task 10: Installs from GitHub

**Files:**
- Create: `components/cyd_update/github_check.h`, `github_check.c`
- Modify: `components/cyd_update/CMakeLists.txt` (PRIV_REQUIRES adds `esp_http_client`), and `cyd_update.c` (the first `cyd_update_network_up()` starts the check task; later calls wake it)

**Interfaces:**
- Consumes: `update_rules.h`, `install.h`, `records.h` and `update_internal.h`, plus `CYD_IMAGE_REPO`.
- Produces: `esp_err_t github_check_start(void);`, which is idempotent.

- [ ] **Step 1: Implement `github_check.c`.**
  - **The task:** stack 8192. It checks at network-up, then every 60 min.
  - **Each check** fetches the release for tag `launcher`, then for the running project's name. The JSON goes into a heap buffer of at most 16 KB, freed after each use.
  - **For each release:**
    1. Call `records_set_seen`.
    2. If `update_release_wanted(commit_time, records_floor(kind))` and the size fits the target, wait, polling every 5 s, until `update_app_interruptible()` and `update_lock()` both allow it, then call `update_screen_take(5000)`. If the screen isn't lent, unlock and poll again.
    3. Download, following up to 5 redirects with the certificate bundle, through `install_*` in 4,096-byte chunks, with progress from source `"GitHub"`.
    4. Call `install_finish` with the parsed digest (or NULL), the release's commit time and the asset's `updated_at`.
    5. The launcher shows `LAUNCHER UPDATED`, and the check goes on to the app. The app shows `RESTARTING` and restarts.
  - **Failures** show `UPDATE FAILED` with a short reason and leave the floor alone, so the next hourly check retries.
- [ ] **Step 2:** Build the dashboard and the launcher. Expected: both succeed. Report the dashboard's size.
- [ ] **Step 3:** Commit: "Install new releases from GitHub by themselves".

### Task 11: Documentation

**Files:**
- Modify: `docs/INSTALL.md`, `README.md`, `launcher/README.md`, `apps/home_dashboard/README.md`, `CLAUDE.md` (approved with this plan)

- [ ] **Step 1: Update `docs/INSTALL.md`.**
  - **The layout table:** the launcher is now published, on the `launcher` tag.
  - **§5:** the update key.
  - **§7:**
    - the one-time USB step: bootloader, launcher and app, written without erasing;
    - `pio run -t upload` now pushes over the network, with `CYD_HOST`, `CYD_KEY_FILE` and `--new-key`;
    - USB flashing with esptool is for recovery.
  - **§8:** the `launcher` release, unchanged images not republished, and the notes lines.
  - **Troubleshooting rows:** `busy`, no key on the board, wrong key, `No route to host` from Python (curl is used), a build that rolled back, and a bad launcher (power-on starts the app; fix over USB).
  - **VS Code:** its upload button runs the same script; macOS may ask once to let VS Code use the local network.
  - **New apps** must include `cyd_update`, or call `esp_ota_mark_app_valid_cancel_rollback()` themselves, or they roll back at their next restart.
- [ ] **Step 2: Update the READMEs** in a few lines each.
- [ ] **Step 3: Update `CLAUDE.md`.**
  - **Settings:** add `update_key`.
  - **Boot:** add rollback and the bad-launcher fallback.
  - **A new Updates bullet:** `cyd_update`, the push and GitHub routes, and floors.
  - **Build:** `pio run -t upload` pushes over the LAN. The launcher and apps still flash over USB with esptool, and `sh test/run.sh` also runs the Python tests.
- [ ] **Step 4:** Run `sh test/run.sh`. Expected: `host tests passed`.
- [ ] **Step 5:** Commit: "Document updates over the network".

### Task 12: Roll out and check on the board (controller with the user, not a subagent)

The board must be at the Mac on USB. Each check names its Review Focus line.

- [ ] **Step 1: The one-time USB step.**
  - Build the launcher and the dashboard from the branch.
  - Flash without erasing: `0x1000` `bootloader.bin`, `0x10000` the launcher's `firmware.bin`, and the dashboard's `firmware.bin` into the slot it now boots from. Read that slot from the serial log or `otadata`.
  - Power-cycle. Expected: the launcher, then the dashboard. The log shows `Build marked good` after 60 s, or no pending state for a USB flash.
- [ ] **Step 2: Set the key.** Run `tools/cyd_push.py --new-key`; it doesn't print the key. The user opens `PHONE SETUP` from the launcher and pastes or types the key into `Update key`. Write `~/.config/cyd/host` with `cyd-e5d0.local`.
- [ ] **Step 3: Push the dashboard.** Run `pio run -t upload` in `apps/home_dashboard`.
  - Expected: `installed home_dashboard in slot X; restarting`, with UPDATING and a percentage on screen, then a restart into the other slot.
  - The next push within 60 s gets `busy N`, and the script retries and installs. **(busy)**
- [ ] **Step 4: Push mid-photo.** Tap for a bird photo and push at once. Expected: no mixed drawing, and the restart comes within 10 s of the transfer ending. **(push mid-photo)**
- [ ] **Step 5: Cut-off push.**
  - Run `CYD_CURL_EXTRA='--max-time 4' pio run -t upload` in `apps/home_dashboard`, so curl gives up mid-transfer.
  - Expected: the board keeps running its build, and the launcher's menu no longer lists the cleared slot after a power-cycle.
  - The next push installs. **(cut-off push)**
- [ ] **Step 6: Wrong key.** Run with `CYD_KEY_FILE` pointing at a second key made with `--new-key --force` into a scratch path. Expected: 403 `wrong key or expired challenge`, and nothing on screen.
- [ ] **Step 7: Rollback.** Build a dashboard with a temporary, uncommitted `abort()` 5 s into `app_main`, and push it.
  - Expected: the board crashes once, then runs the previous build. The log shows `rolled back from slot X`.
  - Revert the change and push the good build.
- [ ] **Step 8: Push the launcher.** Run `pio run -t upload` in `launcher/`. Expected: `LAUNCHER UPDATED`, then back to the readings with no restart. A power-cycle then runs the new launcher.
- [ ] **Step 9 (only with the user's yes): bad-launcher fallback.**
  - Write 4 KB of `0xFF` at `0x10000` with esptool, then power-cycle. Expected: the dashboard starts.
  - Re-flash the launcher at `0x10000`.
- [ ] **Step 10 (after the user approves merging and pushing): the GitHub route.**
  - Merge with `--no-ff`, so the merge commit is newer than the builds on the board.
  - Push, and watch CI with `gh run watch`. Expected: releases `launcher` and `home_dashboard`, with commit notes.
  - Within the hour, or at the next restart, the board installs the launcher (`LAUNCHER UPDATED`) and then the dashboard (`RESTARTING`).
  - A second CI run from a docs-only commit republishes nothing.
