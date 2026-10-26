/* The wildlife camera gallery's GET /frame/wildlife reply: the birds it
   heard last, and the text the dashboard draws for them. */
#ifndef WILDLIFE_H
#define WILDLIFE_H

#include <stdbool.h>
#include <stddef.h>

#define WILDLIFE_MAX_BIRDS 12

/* One bird, in printable ASCII; a field the reply lacks is empty. */
typedef struct {
    char label[48];     /* "Steller's Jay" */
    char time[24];      /* "5:21 PM" today, "Oct 23, 10:52 PM" before */
    char confidence[8]; /* "75%" */
    char photo_url[96]; /* a path on the gallery, "/species_photo/stejay" */
} wildlife_bird_t;

/* Reads up to max birds, newest first, from the reply's birds array.
   Returns how many it stored, or -1 when the reply is not an object with a
   well-formed birds array. A bird without a label that fits is left out. */
int wildlife_parse_birds(const char *json, size_t length, wildlife_bird_t *birds, int max);
/* Writes base_url followed by path into out. Returns false, with out empty,
   when path doesn't start with '/' or the whole doesn't fit. */
bool wildlife_join_url(const char *base_url, const char *path, char *out, size_t size);
/* "Common Raven 5:21 PM" when that fits in columns, else the label alone,
   cut to columns. */
void wildlife_heard_row(const wildlife_bird_t *bird, int columns, char *out, size_t size);

#endif
