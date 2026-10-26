#include "push_auth.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void push_challenge_issue(push_challenge_t *challenge, const uint8_t random[16], int64_t now_us)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        challenge->hex[2 * i] = digits[random[i] >> 4];
        challenge->hex[2 * i + 1] = digits[random[i] & 15];
    }
    challenge->hex[PUSH_CHALLENGE_HEX_LENGTH] = '\0';
    challenge->issued_us = now_us;
    challenge->live = true;
}

/* A wrong guess uses the challenge up too, so one challenge allows one try. The
   compare doesn't stop at the first difference. */
bool push_challenge_take(push_challenge_t *challenge, const char *hex, int64_t now_us)
{
    bool was_live = challenge->live;
    challenge->live = false;
    if (!was_live || strlen(hex) != PUSH_CHALLENGE_HEX_LENGTH) {
        return false;
    }
    unsigned char difference = 0;
    for (int i = 0; i < PUSH_CHALLENGE_HEX_LENGTH; ++i) {
        difference |= (unsigned char)(hex[i] ^ challenge->hex[i]);
    }
    int64_t age = now_us - challenge->issued_us;
    return difference == 0 && age >= 0 && age < PUSH_CHALLENGE_LIFETIME_US;
}

size_t push_auth_message(char *output, size_t capacity, const char *challenge, uint32_t length,
                         const char *sha256_hex, const char *project, int64_t commit_time)
{
    if (strchr(challenge, '\n') != NULL || strchr(sha256_hex, '\n') != NULL ||
        strchr(project, '\n') != NULL) {
        return 0;
    }
    int written = snprintf(output, capacity, "cyd-update-1\n%s\n%" PRIu32 "\n%s\n%s\n%" PRId64 "\n",
                           challenge, length, sha256_hex, project, commit_time);
    if (written <= 0 || (size_t)written >= capacity) {
        return 0;
    }
    return (size_t)written;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool push_auth_parse_hex32(const char *hex, uint8_t output[32])
{
    uint8_t parsed[32];
    for (int i = 0; i < 32; ++i) {
        int high = hex_value(hex[2 * i]);
        if (high < 0) {
            return false; /* also stops at the end of a short string */
        }
        int low = hex_value(hex[2 * i + 1]);
        if (low < 0) {
            return false;
        }
        parsed[i] = (uint8_t)(high << 4 | low);
    }
    if (hex[64] != '\0') {
        return false;
    }
    memcpy(output, parsed, 32);
    return true;
}

bool push_auth_equal(const uint8_t a[32], const uint8_t b[32])
{
    uint8_t difference = 0;
    for (int i = 0; i < 32; ++i) {
        difference |= a[i] ^ b[i];
    }
    return difference == 0;
}
