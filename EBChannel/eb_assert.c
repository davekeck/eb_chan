#include "eb_assert.h"
#include <stdio.h>

void eb_assert_print(const char *msg, const char *cond, const char *file, uintmax_t line, const char *func) {
    fprintf(stderr, "=== %s ===\n"
                    "  Assertion: %s\n"
                    "  File: %s:%ju\n"
                    "  Function: %s\n", msg, cond, file, line, func);
}
