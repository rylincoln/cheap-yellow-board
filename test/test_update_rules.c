#include "check.h"
#include "update_rules.h"

#define NOTES_TEXT \
    "Boot image for apps/home_dashboard, built from 4432784.\n\n" \
    "commit: 4432784aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n" \
    "commit-time: 1793027864\n"
#define NOTES_JSON \
    "Boot image for apps/home_dashboard, built from 4432784.\\n\\n" \
    "commit: 4432784aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\\n" \
    "commit-time: 1793027864\\n"
#define FW_URL "https://github.com/rylincoln/cheap-yellow-board/releases/download/home_dashboard/firmware.bin"
#define NOTES_ASSET \
    "{\"url\": \"https://api.github.com/x/1\", \"name\": \"notes.txt\", \"size\": 120, " \
    "\"digest\": \"sha256:aaaa\", \"updated_at\": \"2026-10-26T14:25:10Z\", " \
    "\"browser_download_url\": \"https://example.invalid/notes.txt\"}"
#define FW_FIELDS \
    "\"url\": \"https://api.github.com/x/2\", \"name\": \"firmware.bin\", \"size\": 1187984, " \
    "\"updated_at\": \"2026-10-26T14:25:17Z\", \"browser_download_url\": \"" FW_URL "\", " \
    "\"uploader\": {\"login\": \"u\", \"id\": 1}"
#define DIGEST "50d50ebfbb1481ef5c8805b3432e1fa990cc35d1c7e67d9c008a38f9ac455800"

/* Wraps a firmware.bin asset's extra fields and a notes body into a reply. */
static void reply(char *out, size_t cap, const char *body, const char *fw_extra)
{
    snprintf(out, cap,
             "{\"url\": \"https://api.github.com/r/1\", \"author\": {\"login\": \"u\", \"id\": 1}, "
             "\"tag_name\": \"home_dashboard\", \"body\": \"%s\", \"assets\": [" NOTES_ASSET ", "
             "{" FW_FIELDS "%s}]}",
             body, fw_extra);
}

static bool parse(const char *json, update_release_t *r)
{
    memset(r, 0, sizeof(*r));
    return update_parse_release(json, strlen(json), r);
}

int main(void)
{
    char notes_q[sizeof(NOTES_TEXT)];
    strcpy(notes_q, NOTES_TEXT);
    for (char *p = notes_q; *p; ++p)
        if (*p == '\n') *p = '?';
    CHECK(update_notes_commit_time(NOTES_TEXT) == 1793027864);
    CHECK(update_notes_commit_time(notes_q) == 1793027864);
    CHECK(update_notes_commit_time("Boot image for apps/home_dashboard, built from 4432784.") == 0);
    CHECK(update_notes_commit_time("commit-time: x") == 0);

    CHECK(update_version_commit_time("1793027864-1dec0ea") == 1793027864);
    CHECK(update_version_commit_time("1793027864-1dec0ea+") == 1793027864);
    CHECK(update_version_commit_time("bluetooth_diag-105-g4432784") == 0);
    CHECK(update_version_commit_time("0-unknown") == 0);
    CHECK(update_version_commit_time("") == 0);
    CHECK(update_version_commit_time("1793027864") == 0);

    CHECK(update_release_wanted(1793027865, 1793027864));
    CHECK(!update_release_wanted(1793027864, 1793027864));
    CHECK(!update_release_wanted(0, 0));
    CHECK(update_release_wanted(5, 0));

    CHECK(update_floor_after_local(100, 200) == 200);
    CHECK(update_floor_after_local(300, 200) == 300);
    CHECK(update_floor_after_local(0, 0) == 0);

    char json[2048];
    update_release_t r;

    /* parsed release */
    reply(json, sizeof(json), NOTES_JSON, ", \"digest\": \"sha256:" DIGEST "\"");
    CHECK(parse(json, &r));
    CHECK_STR(r.url, FW_URL);
    CHECK(r.size == 1187984);
    CHECK_STR(r.sha256, DIGEST);
    CHECK_STR(r.updated_at, "2026-10-26T14:25:17Z");
    CHECK(r.commit_time == 1793027864);

    /* release without firmware.bin */
    snprintf(json, sizeof(json),
             "{\"tag_name\": \"home_dashboard\", \"body\": \"" NOTES_JSON "\", \"assets\": [" NOTES_ASSET "]}");
    CHECK(!parse(json, &r));

    /* no digest */
    reply(json, sizeof(json), NOTES_JSON, "");
    CHECK(parse(json, &r));
    CHECK_STR(r.sha256, "");

    /* sha1 digest */
    reply(json, sizeof(json), NOTES_JSON, ", \"digest\": \"sha1:ab\"");
    CHECK(parse(json, &r));
    CHECK_STR(r.sha256, "");

    /* notes without commit-time */
    reply(json, sizeof(json), "Boot image for apps/home_dashboard, built from 4432784.", "");
    CHECK(parse(json, &r));
    CHECK(r.commit_time == 0);
    CHECK(!update_release_wanted(r.commit_time, 0));

    CHECK_DONE();
}
