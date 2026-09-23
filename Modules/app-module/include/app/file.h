// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <tactility/error.h>
#include <tactility/freertos/freertos.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_FILE_WAIT_READABLE,
    APP_FILE_WAIT_WRITABLE,
} AppFileWait;

#define APP_FILE_READABLE (1u << 0)
#define APP_FILE_WRITABLE (1u << 1)

struct AppFileOps {
    ssize_t (*read)(void* object, void* buffer, size_t size);
    ssize_t (*write)(void* object, const void* buffer, size_t size);
    error_t (*close)(void* object);
    error_t (*await)(void* object, AppFileWait wait, TickType_t timeout);
    uint32_t (*poll)(void* object);
    void (*retain)(void* object);
    void (*release)(void* object);
};

struct AppFile {
    const struct AppFileOps* ops;
    void* object;
    bool suppress_console_tee;
};

#ifdef __cplusplus
}
#endif
