/* The status page a board shows until its first readings arrive: why it
   last started, the Wi-Fi network and its state, the Home Assistant address
   and the last request. */
#ifndef BOARD_STATUS_H
#define BOARD_STATUS_H

#include <stdbool.h>

#include "cyd_display.h"

/* Static buffers, so sized for the widest board; drawing cuts to the panel. */
#define BOARD_STATUS_SIZE (CYD_TEXT_MAX_COLUMNS + 1)

/* Records why this start happened, and what a crashed run was doing. Call
   once, at start. */
void board_status_note_start(void);
/* Says what the board is doing, in the crash record. */
void board_status_set_stage(const char *stage);
void board_status_set_wifi(const char *format, ...) __attribute__((format(printf, 1, 2)));
void board_status_set_ha(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* True when a status changed since the last draw. */
bool board_status_changed(void);
/* Draws the ten rows: title, the start reason, the crash record, the Wi-Fi
   network ssid and state, the address in ha_url and the last request, hint,
   free memory and "tap to retry". */
void board_status_draw(const char *title, const char *ssid, const char *ha_url,
                       const char *hint);

#endif
