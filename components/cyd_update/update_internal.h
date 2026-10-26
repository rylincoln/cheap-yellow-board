/* What the push server and the GitHub check share with the rest of
   cyd_update: one update at a time, and the app's screen while it runs. */
#ifndef UPDATE_INTERNAL_H
#define UPDATE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* The screens' words and timings, from the Shared Values. A detail's lines,
   split at '\n', go on rows 2 and down. */
#define UPDATE_LAUNCHER_LABEL "launcher"
#define UPDATE_TITLE_RESTARTING "RESTARTING"
#define UPDATE_TITLE_LAUNCHER "LAUNCHER UPDATED"
#define UPDATE_DETAIL_LAUNCHER "runs at the next\npower-on"
#define UPDATE_TITLE_FAILED "UPDATE FAILED"
#define UPDATE_RESTARTING_MS 1000
#define UPDATE_LAUNCHER_MS 3000
#define UPDATE_FAILED_MS 3000

bool update_lock(int *busy_seconds);  /* false during the good-start window (60 s minus uptime, at least 1) or another update (30) */
void update_unlock(void);             /* also gives the screen back, or withdraws the request for it; the app redraws only if the update drew */
bool update_screen_take(uint32_t wait_ms);  /* asks screen_task for the screen; true once lent */
void update_screen_progress(const char *label, const char *source, int percent); /* source "Mac" or "GitHub"; draws only while lent */
/* Shows a closing screen for show_ms while lent, then gives the screen back
   unless restarting. UPDATE_TITLE_FAILED is yellow, the others cyan. */
void update_screen_finish(const char *title, const char *detail, uint32_t show_ms, bool restarting);
const char *update_app_label(void);
bool update_app_interruptible(void);

#endif
