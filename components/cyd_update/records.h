/* What the board knows about its images, kept in NVS namespace cyd_update:
   where each partition's image came from, and the floor a GitHub release of
   the launcher or of the running app must pass to be installed. */
#ifndef RECORDS_H
#define RECORDS_H

#include <stdint.h>

#include "esp_partition.h"
#include "image_desc.h"

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

#endif
