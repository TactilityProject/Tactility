#pragma once
#include "Log.h"

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn)) extern void __crash(void);

#define CHECK_GET_MACRO(_1, _2, NAME, ...) NAME
#define check(...) CHECK_GET_MACRO(__VA_ARGS__, CHECK_MSG, CHECK_NO_MSG)(__VA_ARGS__)

#define CHECK_NO_MSG(condition) \
    do { \
        if (!(condition)) { \
            LOG_E("Error", "Check failed: %s at %s:%d", #condition, __FILE__, __LINE__); \
            __crash(); \
        } \
    } while (0)

#define CHECK_MSG(condition, message) \
    do { \
        if (!(condition)) { \
            LOG_E("Error", "Check failed: %s at %s:%d", message, __FILE__, __LINE__); \
            __crash(); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif
