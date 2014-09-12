#include <stdbool.h>
#include <stdint.h>

#define eb_no_op

#define eb_assert_or_recover(cond, action) ({                                                                 \
    if (!(cond)) {                                                                                            \
        eb_assert_print("Assertion failed", #cond, __FILE__, (uintmax_t)__LINE__, __PRETTY_FUNCTION__);       \
        action;                                                                                               \
    }                                                                                                         \
})

#define eb_assert_or_bail(cond, msg) ({                                                        \
    if (!(cond)) {                                                                             \
        eb_assert_print(msg, #cond, __FILE__, (uintmax_t)__LINE__, __PRETTY_FUNCTION__);       \
        abort();                                                                               \
    }                                                                                          \
})

void eb_assert_print(const char *msg, const char *cond, const char *file, uintmax_t line, const char *func);
