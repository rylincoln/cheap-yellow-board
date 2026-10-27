/* The bird screens: the wildlife camera's latest bird, its photo or its
   details, or a message saying why there is none. The app keeps the screen
   it reports for as long as it likes and calls these from one task. */
#ifndef WILDLIFE_VIEW_H
#define WILDLIFE_VIEW_H

/* The wildlife camera's address fits in this, with its NUL. */
#define WILDLIFE_URL_SIZE 128

typedef enum { WILDLIFE_PHOTO, WILDLIFE_INFO, WILDLIFE_MESSAGE } wildlife_screen_t;

/* Says "finding the latest bird" at once, then asks the camera at base_url
   (no trailing slash; empty when not set up) and draws the newest bird's
   photo full screen, or its details when it has no photo, or a message:
   NO WI-FI, NO WILDLIFE CAM, NO BIRDS YET or WILDLIFE ERROR with the reason.
   Blocks for the requests. Returns which of those is on the screen. */
wildlife_screen_t wildlife_show_for_tap(const char *base_url);
/* Draws the details of the birds the last WILDLIFE_PHOTO found, at once and
   without a request. */
void wildlife_show_info(void);

#endif
