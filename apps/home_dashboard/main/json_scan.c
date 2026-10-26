#include "json_scan.h"

#include <stdlib.h>
#include <string.h>

/* What a malformed UTF-8 sequence or lone surrogate decodes to: U+FFFD,
   which has no stand-in, so it is drawn as '?'. */
#define JSON_REPLACEMENT_CHARACTER 0xFFFD

/* Moves the cursor past spaces, tabs and line breaks. */
void json_skip_whitespace(json_cursor_t *cursor)
{
    while (cursor->position < cursor->length &&
           (cursor->data[cursor->position] == ' ' ||
            cursor->data[cursor->position] == '\t' ||
            cursor->data[cursor->position] == '\r' ||
            cursor->data[cursor->position] == '\n')) {
        ++cursor->position;
    }
}

/* The value of one hex digit, or -1. */
static int json_hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/* Reads the four hex digits of a \u escape; -1 if they aren't there. */
static long json_parse_hex4(json_cursor_t *cursor)
{
    if (cursor->length - cursor->position < 4) {
        return -1;
    }
    long codepoint = 0;
    for (int index = 0; index < 4; ++index) {
        int nibble = json_hex_value(cursor->data[cursor->position++]);
        if (nibble < 0) {
            return -1;
        }
        codepoint = (codepoint << 4) | nibble;
    }
    return codepoint;
}

/* Decodes a \u escape whose backslash and u have been read, joining a
   surrogate pair into one code point; -1 if the escape is malformed. A lone
   surrogate comes back as itself, which has no stand-in. */
static long json_parse_unicode_escape(json_cursor_t *cursor)
{
    long codepoint = json_parse_hex4(cursor);
    if (codepoint < 0xD800 || codepoint > 0xDBFF) {
        return codepoint;
    }
    /* A high surrogate: the low one should follow as the next escape. */
    if (cursor->length - cursor->position >= 6 && cursor->data[cursor->position] == '\\' &&
        cursor->data[cursor->position + 1] == 'u') {
        json_cursor_t low_cursor = *cursor;
        low_cursor.position += 2;
        long low = json_parse_hex4(&low_cursor);
        if (low >= 0xDC00 && low <= 0xDFFF) {
            *cursor = low_cursor;
            return 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        }
    }
    return codepoint;
}

/* Decodes the raw UTF-8 sequence whose lead byte has been read. A malformed
   one gives JSON_REPLACEMENT_CHARACTER, after consuming the lead byte and
   the continuation bytes that follow it. */
static long json_parse_utf8(json_cursor_t *cursor, unsigned char lead)
{
    int continuation_bytes;
    long codepoint;
    long smallest;
    if (lead >= 0xC2 && lead <= 0xDF) {
        continuation_bytes = 1;
        codepoint = lead & 0x1F;
        smallest = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        continuation_bytes = 2;
        codepoint = lead & 0x0F;
        smallest = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        continuation_bytes = 3;
        codepoint = lead & 0x07;
        smallest = 0x10000;
    } else {
        /* A stray continuation byte, or a lead byte no sequence uses. */
        return JSON_REPLACEMENT_CHARACTER;
    }
    for (int index = 0; index < continuation_bytes; ++index) {
        if (cursor->position >= cursor->length ||
            ((unsigned char)cursor->data[cursor->position] & 0xC0) != 0x80) {
            return JSON_REPLACEMENT_CHARACTER;
        }
        codepoint = (codepoint << 6) | ((unsigned char)cursor->data[cursor->position++] & 0x3F);
    }
    /* Overlong forms, surrogates and code points past U+10FFFF. */
    if (codepoint < smallest || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return JSON_REPLACEMENT_CHARACTER;
    }
    return codepoint;
}

/* The printable ASCII character drawn for a code point: itself, a
   look-alike for curly quotes, dashes and the no-break space, else '?'. */
static char json_ascii_stand_in(long codepoint)
{
    if (codepoint >= 0x20 && codepoint <= 0x7E) {
        return (char)codepoint;
    }
    switch (codepoint) {
    case 0x2018: /* left single quotation mark */
    case 0x2019: /* right single quotation mark */
    case 0x02BC: /* modifier letter apostrophe */
        return '\'';
    case 0x201C: /* left double quotation mark */
    case 0x201D: /* right double quotation mark */
        return '"';
    case 0x2013: /* en dash */
    case 0x2014: /* em dash */
        return '-';
    case 0x00A0: /* no-break space */
        return ' ';
    default:
        return '?';
    }
}

/* Reads the string at the cursor into output, cut to capacity - 1
   characters, each one printable ASCII; output may be NULL to skip it. */
bool json_parse_string(json_cursor_t *cursor, char *output, size_t capacity)
{
    if (cursor->position >= cursor->length || cursor->data[cursor->position++] != '"') {
        return false;
    }
    size_t written = 0;
    while (cursor->position < cursor->length) {
        unsigned char character = (unsigned char)cursor->data[cursor->position++];
        if (character == '"') {
            if (output != NULL && capacity > 0) {
                output[written] = '\0';
            }
            return true;
        }
        if (character < 0x20) {
            return false;
        }
        long codepoint = character;
        if (character == '\\') {
            if (cursor->position >= cursor->length) {
                return false;
            }
            character = (unsigned char)cursor->data[cursor->position++];
            if (character == 'u') {
                codepoint = json_parse_unicode_escape(cursor);
                if (codepoint < 0) {
                    return false;
                }
            } else if (character == '"' || character == '\\' || character == '/') {
                /* Keep the escaped character as-is. */
                codepoint = character;
            } else if (character == 'b') {
                codepoint = '\b';
            } else if (character == 'f') {
                codepoint = '\f';
            } else if (character == 'n') {
                codepoint = '\n';
            } else if (character == 'r') {
                codepoint = '\r';
            } else if (character == 't') {
                codepoint = '\t';
            } else {
                return false;
            }
        } else if (character >= 0x80) {
            codepoint = json_parse_utf8(cursor, character);
        }
        if (output != NULL && capacity > 1 && written < capacity - 1) {
            output[written++] = json_ascii_stand_in(codepoint);
        }
    }
    return false;
}

/* Moves the cursor past one value of any kind, nested at most 16 deep. */
bool json_skip_value(json_cursor_t *cursor, unsigned int depth)
{
    if (depth > 16) {
        return false;
    }
    json_skip_whitespace(cursor);
    if (cursor->position >= cursor->length) {
        return false;
    }
    char first = cursor->data[cursor->position];
    if (first == '"') {
        return json_parse_string(cursor, NULL, 0);
    }
    if (first == '{') {
        ++cursor->position;
        json_skip_whitespace(cursor);
        if (cursor->position < cursor->length && cursor->data[cursor->position] == '}') {
            ++cursor->position;
            return true;
        }
        while (cursor->position < cursor->length) {
            if (!json_parse_string(cursor, NULL, 0)) {
                return false;
            }
            json_skip_whitespace(cursor);
            if (cursor->position >= cursor->length || cursor->data[cursor->position++] != ':') {
                return false;
            }
            if (!json_skip_value(cursor, depth + 1)) {
                return false;
            }
            json_skip_whitespace(cursor);
            if (cursor->position >= cursor->length) {
                return false;
            }
            char delimiter = cursor->data[cursor->position++];
            if (delimiter == '}') {
                return true;
            }
            if (delimiter != ',') {
                return false;
            }
            json_skip_whitespace(cursor);
        }
        return false;
    }
    if (first == '[') {
        ++cursor->position;
        json_skip_whitespace(cursor);
        if (cursor->position < cursor->length && cursor->data[cursor->position] == ']') {
            ++cursor->position;
            return true;
        }
        while (cursor->position < cursor->length) {
            if (!json_skip_value(cursor, depth + 1)) {
                return false;
            }
            json_skip_whitespace(cursor);
            if (cursor->position >= cursor->length) {
                return false;
            }
            char delimiter = cursor->data[cursor->position++];
            if (delimiter == ']') {
                return true;
            }
            if (delimiter != ',') {
                return false;
            }
        }
        return false;
    }

    size_t start = cursor->position;
    while (cursor->position < cursor->length) {
        char character = cursor->data[cursor->position];
        if (character == ',' || character == ']' || character == '}' ||
            character == ' ' || character == '\t' ||
            character == '\r' || character == '\n') {
            break;
        }
        ++cursor->position;
    }
    return cursor->position > start;
}

/* Finds the member field_name of the object between start and end. */
bool json_object_field(const char *data, size_t start, size_t end,
                       const char *field_name, json_value_t *value)
{
    json_cursor_t cursor = {.data = data, .length = end, .position = start};
    json_skip_whitespace(&cursor);
    if (cursor.position >= end || cursor.data[cursor.position++] != '{') {
        return false;
    }
    json_skip_whitespace(&cursor);
    if (cursor.position < end && cursor.data[cursor.position] == '}') {
        return false;
    }
    while (cursor.position < end) {
        char key[48];
        if (!json_parse_string(&cursor, key, sizeof(key))) {
            return false;
        }
        json_skip_whitespace(&cursor);
        if (cursor.position >= end || cursor.data[cursor.position++] != ':') {
            return false;
        }
        json_skip_whitespace(&cursor);
        size_t value_start = cursor.position;
        if (!json_skip_value(&cursor, 0)) {
            return false;
        }
        size_t value_end = cursor.position;
        if (strcmp(key, field_name) == 0) {
            value->start = value_start;
            value->end = value_end;
            return true;
        }
        json_skip_whitespace(&cursor);
        if (cursor.position >= end) {
            return false;
        }
        char delimiter = cursor.data[cursor.position++];
        if (delimiter == '}') {
            return false;
        }
        if (delimiter != ',') {
            return false;
        }
        json_skip_whitespace(&cursor);
    }
    return false;
}

/* Reads value into output when it is a string. */
bool json_value_string(const char *data, json_value_t value,
                       char *output, size_t capacity)
{
    json_cursor_t cursor = {
        .data = data, .length = value.end, .position = value.start,
    };
    return json_parse_string(&cursor, output, capacity) && cursor.position == value.end;
}

/* Reads value into number when it is a number. */
bool json_value_number(const char *data, json_value_t value, double *number)
{
    char buffer[40];
    size_t length = value.end - value.start;
    if (length == 0 || length >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, data + value.start, length);
    buffer[length] = '\0';
    char *end;
    *number = strtod(buffer, &end);
    return end != buffer && *end == '\0';
}

/* Reads the string member key of the object between start and end into
   output; output is empty when there is none. */
bool json_object_string(const char *data, size_t start, size_t end,
                        const char *key, char *output, size_t capacity)
{
    json_value_t value;
    output[0] = '\0';
    return json_object_field(data, start, end, key, &value) &&
           json_value_string(data, value, output, capacity);
}
