#include "eb_assert.h"
#include <stdio.h>

void eb_assert_print(const char *msg, const char *file, uintmax_t line, const char *func, const char *cond) {
    fprintf(stderr, "=== %s ===\n"
                    "  File: %s:%ju\n"
                    "  Function: %s\n"
                    "  Assertion: %s\n", msg, file, line, func, cond);
}
