/* When an image from a GitHub release should be installed, and how to read
   GitHub's reply about a release. Pure C, so it builds on the host. */
#ifndef UPDATE_RULES_H
#define UPDATE_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char url[256];        /* firmware.bin's browser_download_url */
    uint32_t size;
    char sha256[65];      /* hex from the asset's "digest": "sha256:<hex>", else empty */
    char updated_at[24];  /* the asset's updated_at */
    int64_t commit_time;  /* the notes' "commit-time:" value, else 0 */
} update_release_t;

/* From GitHub's reply to GET .../releases/tags/<tag>; false when there is no firmware.bin asset. */
bool update_parse_release(const char *json, size_t length, update_release_t *release);
/* The digits after "commit-time: " in release notes, else 0. */
int64_t update_notes_commit_time(const char *notes);
/* The digits before the '-' of an image version, else 0. */
int64_t update_version_commit_time(const char *version);
/* A release installs only when its commit time is above 0 and above the floor. */
bool update_release_wanted(int64_t release_time, int64_t floor);
/* The floor after the running image was installed by other means. */
int64_t update_floor_after_local(int64_t image_time, int64_t last_seen_release_time);

#endif
