#include "wildlife.h"

#include <stdio.h>
#include <string.h>

#include "json_scan.h"

/* Copies the string member name of the bird object into out. Returns false,
   with out empty, when the member is missing, isn't a string, or is longer
   than size - 1 characters once decoded. */
static bool copy_member(const char *json, json_value_t bird, const char *name, char *out,
                        size_t size)
{
    char text[sizeof(((wildlife_bird_t *)0)->photo_url) + 1]; /* a character past the longest */
    json_value_t value;
    out[0] = '\0';
    if (size >= sizeof(text) || !json_object_field(json, bird.start, bird.end, name, &value) ||
        !json_value_string(json, value, text, size + 1) || strlen(text) >= size) {
        return false;
    }
    memcpy(out, text, strlen(text) + 1);
    return true;
}

/* Reads one element of the birds array into bird; false when it has no
   label that fits, so it is left out. */
static bool read_bird(const char *json, json_value_t element, wildlife_bird_t *bird)
{
    if (!copy_member(json, element, "label", bird->label, sizeof(bird->label)) ||
        bird->label[0] == '\0') {
        return false;
    }
    copy_member(json, element, "time", bird->time, sizeof(bird->time));
    copy_member(json, element, "confidence", bird->confidence, sizeof(bird->confidence));
    if (!copy_member(json, element, "photo_url", bird->photo_url, sizeof(bird->photo_url)) ||
        bird->photo_url[0] != '/') {
        bird->photo_url[0] = '\0';
    }
    return true;
}

/* Reads the birds array at the cursor, up to max birds. Returns how many it
   stored, or -1 when the array is cut off or malformed. */
static int read_bird_array(json_cursor_t *cursor, wildlife_bird_t *birds, int max)
{
    int count = 0;
    ++cursor->position; /* the '[' */
    json_skip_whitespace(cursor);
    if (cursor->position < cursor->length && cursor->data[cursor->position] == ']') {
        return 0;
    }
    while (count < max) {
        json_skip_whitespace(cursor);
        json_value_t element = {.start = cursor->position};
        if (!json_skip_value(cursor, 0)) {
            return -1;
        }
        element.end = cursor->position;
        wildlife_bird_t bird;
        if (read_bird(cursor->data, element, &bird)) {
            birds[count++] = bird;
        }
        json_skip_whitespace(cursor);
        if (cursor->position >= cursor->length) {
            return -1;
        }
        char delimiter = cursor->data[cursor->position++];
        if (delimiter == ']') {
            return count;
        }
        if (delimiter != ',') {
            return -1;
        }
    }
    return count;
}

/* Reads up to max birds from the reply's birds array. Other members before
   it are skipped whole; reading ends with the array, so nothing after it is
   needed. */
int wildlife_parse_birds(const char *json, size_t length, wildlife_bird_t *birds, int max)
{
    json_cursor_t cursor = {.data = json, .length = length, .position = 0};
    json_skip_whitespace(&cursor);
    if (cursor.position >= length || json[cursor.position++] != '{') {
        return -1;
    }
    json_skip_whitespace(&cursor);
    if (cursor.position < length && json[cursor.position] == '}') {
        return -1;
    }
    while (cursor.position < length) {
        char key[16];
        if (!json_parse_string(&cursor, key, sizeof(key))) {
            return -1;
        }
        json_skip_whitespace(&cursor);
        if (cursor.position >= length || json[cursor.position++] != ':') {
            return -1;
        }
        json_skip_whitespace(&cursor);
        if (strcmp(key, "birds") == 0) {
            if (cursor.position >= length || json[cursor.position] != '[') {
                return -1;
            }
            return read_bird_array(&cursor, birds, max);
        }
        if (!json_skip_value(&cursor, 0)) {
            return -1;
        }
        json_skip_whitespace(&cursor);
        if (cursor.position >= length || json[cursor.position++] != ',') {
            return -1; /* the object ended, or broke off, without birds */
        }
        json_skip_whitespace(&cursor);
    }
    return -1;
}

/* Writes base_url followed by path into out, when path is a path and the
   whole fits. */
bool wildlife_join_url(const char *base_url, const char *path, char *out, size_t size)
{
    if (size == 0) {
        return false;
    }
    out[0] = '\0';
    if (path[0] != '/') {
        return false;
    }
    int length = snprintf(out, size, "%s%s", base_url, path);
    if (length < 0 || (size_t)length >= size) {
        out[0] = '\0';
        return false;
    }
    return true;
}

/* The bird's label and time when both fit in columns, else its label cut
   to columns. */
void wildlife_heard_row(const wildlife_bird_t *bird, int columns, char *out, size_t size)
{
    size_t room = columns > 0 ? (size_t)columns : 0;
    size_t label_length = strlen(bird->label);
    size_t time_length = strlen(bird->time);
    if (time_length > 0 && label_length + 1 + time_length <= room) {
        snprintf(out, size, "%s %s", bird->label, bird->time);
    } else {
        snprintf(out, size, "%.*s", (int)room, bird->label);
    }
}
