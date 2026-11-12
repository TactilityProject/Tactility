#pragma once

#include <new>

struct Device;

/**
 * Common driver-data wrapper attached to every LVGL display/indev created by this module: bundles
 * the kernel Device* with an optional device-type-specific context blob (LvglDisplayCtx,
 * LvglPointerCtx, ...), so each device type doesn't have to store its own Device*
 * redundantly. Owns context: any resources referenced through it (buffers, cursor objects, etc.)
 * must be released by the caller before the wrapper is deleted, since context is trivially
 * destructible and only its own memory is freed here.
 */
struct LvglDeviceContext {
    struct Device* device = nullptr;
    void* context;

    explicit LvglDeviceContext(void* context) : context(context) {}

    ~LvglDeviceContext() {
        if (context != nullptr) {
            ::operator delete(context);
        }
    }
};
