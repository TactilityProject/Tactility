// SPDX-License-Identifier: Apache-2.0
#include <app/private/null_device.h>

namespace {

ssize_t null_read(void*, void*, size_t) {
    return 0; // EOF
}

ssize_t null_write(void*, const void* buffer, size_t size) {
    (void)buffer;
    return static_cast<ssize_t>(size); // discarded, reported as fully written
}

error_t null_close(void*) {
    return ERROR_NONE;
}

error_t null_await(void*, AppFileWait, TickType_t) {
    return ERROR_NONE; // always ready
}

uint32_t null_poll(void*) {
    return APP_FILE_READABLE | APP_FILE_WRITABLE;
}

const AppFileOps NULL_OPS = {
    .read = null_read,
    .write = null_write,
    .close = null_close,
    .await = null_await,
    .poll = null_poll,
    .retain = nullptr,
    .release = nullptr,
};

constexpr AppFile NULL_FILE = { .ops = &NULL_OPS, .object = nullptr };

} // namespace

extern "C" {

const AppFile* app_null_file(void) {
    return &NULL_FILE;
}

} // extern "C"
