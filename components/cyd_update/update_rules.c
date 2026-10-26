#include "update_rules.h"

#include <ctype.h>
#include <string.h>

#include "json_scan.h"

/* Reads the digits at text as a number; 0 when there are none or too many. */
static int64_t read_digits(const char *text)
{
    int64_t number = 0;
    int count = 0;
    while (isdigit((unsigned char)*text)) {
        if (++count > 15) return 0;
        number = number * 10 + (*text++ - '0');
    }
    return number;
}

/* Finds the number after "commit-time: " by text, not by line, because the
   JSON scanner turns the notes' line breaks into '?'. */
int64_t update_notes_commit_time(const char *notes)
{
    const char *at = strstr(notes, "commit-time: ");
    return at ? read_digits(at + strlen("commit-time: ")) : 0;
}

int64_t update_version_commit_time(const char *version)
{
    size_t digits = strspn(version, "0123456789");
    if (digits == 0 || version[digits] != '-') return 0;
    return read_digits(version);
}

bool update_release_wanted(int64_t release_time, int64_t floor)
{
    return release_time > 0 && release_time > floor;
}

int64_t update_floor_after_local(int64_t image_time, int64_t last_seen_release_time)
{
    return image_time > last_seen_release_time ? image_time : last_seen_release_time;
}

/* Fills release from the asset object between start and end. */
static void read_asset(const char *json, size_t start, size_t end, update_release_t *release)
{
    json_value_t value;
    double number;
    char digest[80];

    json_object_string(json, start, end, "browser_download_url", release->url, sizeof(release->url));
    json_object_string(json, start, end, "updated_at", release->updated_at, sizeof(release->updated_at));
    if (json_object_field(json, start, end, "size", &value) && json_value_number(json, value, &number) &&
        number >= 0 && number <= 4294967295.0)
        release->size = (uint32_t)number;
    json_object_string(json, start, end, "digest", digest, sizeof(digest));
    if (strncmp(digest, "sha256:", 7) == 0 && strlen(digest + 7) == 64)
        strcpy(release->sha256, digest + 7);
}

bool update_parse_release(const char *json, size_t length, update_release_t *release)
{
    json_value_t body, assets;
    char notes[512];
    bool found = false;

    memset(release, 0, sizeof(*release));
    if (json_object_field(json, 0, length, "body", &body) && json_value_string(json, body, notes, sizeof(notes)))
        release->commit_time = update_notes_commit_time(notes);

    if (!json_object_field(json, 0, length, "assets", &assets)) return false;
    json_cursor_t cursor = {json, assets.end, assets.start};
    json_skip_whitespace(&cursor);
    if (cursor.position >= cursor.length || json[cursor.position] != '[') return false;
    ++cursor.position;
    for (;;) {
        json_skip_whitespace(&cursor);
        if (cursor.position >= cursor.length || json[cursor.position] == ']') break;
        size_t start = cursor.position;
        if (!json_skip_value(&cursor, 1)) break;
        char name[32];
        json_object_string(json, start, cursor.position, "name", name, sizeof(name));
        if (strcmp(name, "firmware.bin") == 0) {
            read_asset(json, start, cursor.position, release);
            found = true;
            break;
        }
        json_skip_whitespace(&cursor);
        if (cursor.position < cursor.length && json[cursor.position] == ',') ++cursor.position;
    }
    if (!found) memset(release, 0, sizeof(*release));
    return found;
}
