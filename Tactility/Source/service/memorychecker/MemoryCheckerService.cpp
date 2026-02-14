#include <Tactility/Logger.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/memorychecker/MemoryCheckerService.h>

#include <tactility/lvgl_symbols_statusbar.h>

namespace tt::service::memorychecker {

static const auto LOGGER = Logger("MemoryChecker");

// Total memory (in bytes) that should be free before warnings occur
constexpr auto TOTAL_FREE_THRESHOLD = 10'000;
// Smallest memory block size (in bytes) that should be available before warnings occur
constexpr auto LARGEST_FREE_BLOCK_THRESHOLD = 2'000;

static size_t getInternalFree() {
#ifdef ESP_PLATFORM
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#else
    // PC mock data
    return 1024 * 1024;
#endif
}

static size_t getInternalLargestFreeBlock() {
#ifdef ESP_PLATFORM
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
#else
    // PC mock data
    return 1024 * 1024;
#endif
}

static bool isMemoryLow() {
    bool memory_low = false;
    const auto total_free = getInternalFree();
    if (total_free < TOTAL_FREE_THRESHOLD) {
        LOGGER.warn("Internal memory low: {} bytes", total_free);
        memory_low = true;
    }

    const auto largest_block = getInternalLargestFreeBlock();
    if (largest_block < LARGEST_FREE_BLOCK_THRESHOLD) {
        LOGGER.warn("Largest free internal memory block is {} bytes", largest_block);
        memory_low = true;
    }

    return memory_low;
}

bool MemoryCheckerService::onStart(ServiceContext& service) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    statusbarIconId = lvgl::statusbar_icon_add(LVGL_SYMBOL_MEMORY, false);
    lvgl::statusbar_icon_set_visibility(statusbarIconId, false);

    timer.setCallbackPriority(Thread::Priority::Lower);
    timer.start();

    return true;
}

void MemoryCheckerService::onStop(ServiceContext& service) {
    timer.stop();

    // Lock after timer stop, because the timer task might still be busy and this way we await it
    auto lock = mutex.asScopedLock();
    lock.lock();

    lvgl::statusbar_icon_remove(statusbarIconId);
}

void MemoryCheckerService::onTimerUpdate() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    bool memory_low = isMemoryLow();
    if (memory_low != memoryLow) {
        memoryLow = memory_low;
        lvgl::statusbar_icon_set_visibility(statusbarIconId, memory_low);
    }
}

extern const ServiceManifest manifest = {
    .id = "MemoryChecker",
    .createService = create<MemoryCheckerService>
};

}
