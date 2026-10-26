#ifndef FORM_CHECK_H
#define FORM_CHECK_H

#include <stdbool.h>
#include <stddef.h>

/* Empty for an open network, 8 to 63 characters, or 64 hex digits. */
bool form_password_valid(const char *password);

/* Empty, or an http(s) address; a bare host name gets "http://". */
bool form_url_valid(char *url, size_t size);

/* Empty, or "domain.object_id": two non-empty parts of [a-z0-9_] around one dot. */
bool form_entity_id_valid(const char *entity_id);

/* Strips leading and trailing ASCII whitespace in place. */
void form_trim(char *text);

/* True when the token field was left empty and the URL is unchanged, so the saved token stays. */
bool form_token_kept(const char *token, const char *ha_url, const char *saved_ha_url);

/* Rewrites an update key as the board stores it: spaces and dashes dropped,
   uppercase, O read as 0 and I or L as 1. True when the result is empty (blank
   keeps the saved key) or 20 characters of 0-9 A-Z without I, L, O and U. */
bool form_update_key_normalize(char *key);

#endif
