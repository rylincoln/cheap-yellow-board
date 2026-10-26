/* Takes builds pushed from the Mac over HTTP, and names the board on the
   network with mDNS so the Mac can find it. */
#ifndef PUSH_SERVER_H
#define PUSH_SERVER_H

#include "esp_err.h"

/* Starts the server on port 80, then mDNS; ESP_OK once both run. Calling it
   again does nothing for what already runs and retries what didn't start.
   One caller at a time. */
esp_err_t push_server_start(void);

#endif
