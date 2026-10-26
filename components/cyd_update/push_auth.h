#ifndef PUSH_AUTH_H
#define PUSH_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PUSH_CHALLENGE_HEX_LENGTH 32
#define PUSH_CHALLENGE_LIFETIME_US (60LL * 1000 * 1000)

typedef struct {
    char hex[PUSH_CHALLENGE_HEX_LENGTH + 1];
    int64_t issued_us;
    bool live;
} push_challenge_t;

/* Replaces any earlier challenge with one made of the 16 random bytes. */
void push_challenge_issue(push_challenge_t *challenge, const uint8_t random[16], int64_t now_us);
/* True once: hex matches, it is under the lifetime old, and unused. Any call uses it up. */
bool push_challenge_take(push_challenge_t *challenge, const char *hex, int64_t now_us);
/* The text the HMAC covers; returns its length, or 0 when it doesn't fit or a field holds '\n'. */
size_t push_auth_message(char *output, size_t capacity, const char *challenge, uint32_t length,
                         const char *sha256_hex, const char *project, int64_t commit_time);
bool push_auth_parse_hex32(const char *hex, uint8_t output[32]); /* exactly 64 hex digits, either case */
bool push_auth_equal(const uint8_t a[32], const uint8_t b[32]);  /* constant time */

#endif
