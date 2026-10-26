/* Installs new GitHub releases of the launcher and of the running app by
   themselves. */
#ifndef GITHUB_CHECK_H
#define GITHUB_CHECK_H

#include "esp_err.h"

/* Starts the task that checks GitHub now, then every 60 minutes. A later
   call starts no second task: it wakes the one running to check again
   without waiting out the hour, as when Wi-Fi comes back. One caller at a
   time. */
esp_err_t github_check_start(void);

#endif
