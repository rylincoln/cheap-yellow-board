#pragma once

/* Updates for a CYD boot image. A new build is kept only after a good start:
   it has run for a minute and drawn its first screen. Until then a restart
   goes back to the build before it.

   An update borrows the app's screen while it installs. The task that draws
   the app's screens calls cyd_update_lend_screen when screen_bit is notified,
   then redraws its screen in full. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
