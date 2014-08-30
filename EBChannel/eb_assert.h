#include <stdbool.h>
#include <stdint.h>

#define eb_no_op

#define eb_assert_or_recover(cond, action) ({                                                                 \
    if (!(cond)) {                                                                                            \
        eb_assert_print("Assertion failed", __FILE__, (uintmax_t)__LINE__, __PRETTY_FUNCTION__, #cond);       \
        action;                                                                                               \
    }                                                                                                         \
})

#define eb_assert_or_bail(cond, msg) ({                                                        \
    if (!(cond)) {                                                                             \
        eb_assert_print(msg, __FILE__, (uintmax_t)__LINE__, __PRETTY_FUNCTION__, #cond);       \
        abort();                                                                               \
    }                                                                                          \
})

void eb_assert_print(const char *msg, const char *file, uintmax_t line, const char *func, const char *cond);
