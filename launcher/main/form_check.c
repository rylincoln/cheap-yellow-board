#include "form_check.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

/* Empty for an open network, 8 to 63 characters, or 64 hex digits. */
bool form_password_valid(const char *password)
{
    size_t length = strlen(password);
    if (length == 0 || (length >= 8 && length <= 63)) {
        return true;
    }
    for (size_t i = 0; i < length; ++i) {
        if (!isxdigit((unsigned char)password[i])) {
            return false;
        }
    }
    return length == 64;
}

/* Empty, or an http(s) address; a bare host name gets "http://". */
bool form_url_valid(char *url, size_t size)
{
    size_t length = strlen(url);
    if (length == 0) {
        return true;
    }
    if (strchr(url, ' ') != NULL) {
        return false;
    }
    if (strstr(url, "://") == NULL) {
        if (length + 7 >= size) {
            return false;
        }
        memmove(url + 7, url, length + 1);
        memcpy(url, "http://", 7);
    }
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

/* Empty, or "domain.object_id": two non-empty parts of [a-z0-9_] around one dot. */
bool form_entity_id_valid(const char *entity_id)
{
    if (entity_id[0] == '\0') {
        return true;
    }
    size_t dots = 0;
    size_t part_length = 0;
    for (const char *c = entity_id; *c != '\0'; ++c) {
        if (*c == '.') {
            if (part_length == 0) {
                return false;
            }
            ++dots;
            part_length = 0;
        } else if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_') {
            ++part_length;
        } else {
            return false;
        }
    }
    return dots == 1 && part_length > 0;
}

/* Space, tab, newline, vertical tab, form feed or carriage return. */
static bool is_ascii_space(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/* Strips leading and trailing ASCII whitespace in place. */
void form_trim(char *text)
{
    size_t start = 0;
    while (is_ascii_space(text[start])) {
        ++start;
    }
    size_t end = strlen(text + start) + start;
    while (end > start && is_ascii_space(text[end - 1])) {
        --end;
    }
    memmove(text, text + start, end - start);
    text[end - start] = '\0';
}

/* True when the token field was left empty and the URL is unchanged, so the saved token stays. */
bool form_token_kept(const char *token, const char *ha_url, const char *saved_ha_url)
{
    return token[0] == '\0' && strcmp(ha_url, saved_ha_url) == 0;
}

/* Rewrites an update key as the board stores it; true when blank or valid. */
bool form_update_key_normalize(char *key)
{
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    size_t length = 0;
    for (const char *in = key; *in != '\0'; ++in) {
        char c = *in;
        if (c == '-' || is_ascii_space(c)) {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        c = c == 'O' ? '0' : (c == 'I' || c == 'L') ? '1' : c;
        key[length++] = c;
    }
    key[length] = '\0';
    if (length == 0) {
        return true;
    }
    if (length != 20) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (strchr(alphabet, key[i]) == NULL) {
            return false;
        }
    }
    return true;
}
