/* Minimal host-test helpers: CHECK, CHECK_STR and CHECK_DONE. */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>

static int check_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
            ++check_failures;                                                \
        }                                                                    \
    } while (0)

#define CHECK_STR(actual, expected)                                          \
    do {                                                                     \
        const char *check_actual = (actual);                                 \
        const char *check_expected = (expected);                             \
        if (strcmp(check_actual, check_expected) != 0) {                     \
            printf("%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n", __FILE__,  \
                   __LINE__, check_actual, check_expected);                  \
            ++check_failures;                                                \
        }                                                                    \
    } while (0)

/* Ends main: nonzero status if any check failed. */
#define CHECK_DONE() return check_failures != 0

#endif
