/* A small JSON scanner for replies held in memory: it finds values in
   place, without allocating, and decodes strings to printable ASCII. */
#ifndef JSON_SCAN_H
#define JSON_SCAN_H

#include <stdbool.h>
#include <stddef.h>

/* A position in a document of length bytes, which need not end in a NUL. */
typedef struct {
    const char *data;
    size_t length;
    size_t position;
} json_cursor_t;

/* Where a value lies in its document: from data[start] up to data[end]. */
typedef struct {
    size_t start;
    size_t end;
} json_value_t;

/* Moves the cursor past spaces, tabs and line breaks. */
void json_skip_whitespace(json_cursor_t *cursor);
/* Reads the string at the cursor into output, cut to capacity - 1
   characters; output may be NULL to skip it. Every character comes out as
   printable ASCII: curly quotes, en and em dashes and the no-break space
   become their ASCII look-alikes, and any other character outside 0x20 to
   0x7E, or a malformed UTF-8 sequence, becomes one '?'. */
bool json_parse_string(json_cursor_t *cursor, char *output, size_t capacity);
/* Moves the cursor past one value of any kind, nested at most 16 deep. */
bool json_skip_value(json_cursor_t *cursor, unsigned int depth);
/* Finds the member field_name of the object between start and end. */
bool json_object_field(const char *data, size_t start, size_t end,
                       const char *field_name, json_value_t *value);
/* Reads value into output when it is a string. */
bool json_value_string(const char *data, json_value_t value,
                       char *output, size_t capacity);
/* Reads value into number when it is a number. */
bool json_value_number(const char *data, json_value_t value, double *number);
/* Reads the string member key of the object between start and end into
   output; output is empty when there is none. */
bool json_object_string(const char *data, size_t start, size_t end,
                        const char *key, char *output, size_t capacity);

#endif
