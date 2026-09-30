// SPDX-License-Identifier: Apache-2.0
#include <sdkconfig.h>

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM

#include <esp_ipc.h>
#include <freertos/FreeRTOS.h>

#include <atomic>

// elf_loader writes back the whole D-cache with the ROM's Cache_WriteBack_All(), which doesn't take
// esp_cache_msync()'s lock. A cache operation running at the same time on the other core (e.g. a display
// driver syncing a PSRAM framebuffer) then fails with "Dcache sync parameter configuration error".
// The write-back therefore runs in core 0's IPC task while core 1 is parked in its own IPC task. An IPC task
// only runs when its core is not in a critical section, which is where esp_cache_msync() does its work.
extern "C" {

void __real_esp_elf_arch_flush(void);
void Cache_WriteBack_All(void);
void spi_flash_disable_interrupts_caches_and_other_cpu(void);
void spi_flash_enable_interrupts_caches_and_other_cpu(void);

}

#if !CONFIG_FREERTOS_UNICORE && !CONFIG_IDF_TARGET_ESP32S31

namespace {

std::atomic<bool> other_core_parked { false };
std::atomic<bool> other_core_release { false };

void IRAM_ATTR park_core(void*) {
    const uint32_t state = portSET_INTERRUPT_MASK_FROM_ISR();
    other_core_parked.store(true, std::memory_order_release);
    while (!other_core_release.load(std::memory_order_acquire)) {
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR(state);
}

void IRAM_ATTR write_back_with_core1_parked(void*) {
    other_core_parked.store(false, std::memory_order_relaxed);
    other_core_release.store(false, std::memory_order_relaxed);
    esp_ipc_call(1, park_core, nullptr);
    while (!other_core_parked.load(std::memory_order_acquire)) {
    }
    const uint32_t state = portSET_INTERRUPT_MASK_FROM_ISR();
    Cache_WriteBack_All();
    portCLEAR_INTERRUPT_MASK_FROM_ISR(state);
    other_core_release.store(true, std::memory_order_release);
}

} // namespace

// IRAM: spi_flash_disable_interrupts_caches_and_other_cpu() returns here with the caches disabled
extern "C" void IRAM_ATTR __wrap_esp_elf_arch_flush(void) {
    esp_ipc_call_blocking(0, write_back_with_core1_parked, nullptr);
    // Same as elf_loader's own esp_elf_arch_flush() after its write-back
    spi_flash_disable_interrupts_caches_and_other_cpu();
    spi_flash_enable_interrupts_caches_and_other_cpu();
}

#else

extern "C" void IRAM_ATTR __wrap_esp_elf_arch_flush(void) {
    __real_esp_elf_arch_flush();
}

#endif

#endif // CONFIG_ELF_LOADER_LOAD_PSRAM
