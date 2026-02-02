#include <private/elf_symbol.h>
#include <cstddef>

#include <symbols/cplusplus.h>

extern "C" {
    extern void* _Znwj(uint32_t size); // operator new(unsigned int)
    extern void _ZdlPvj(void* p, uint64_t size); // operator delete(void*, unsigned int)
    extern void __cxa_pure_virtual();
    // cxx_guards.cpp
    extern int __cxa_guard_acquire(void* pg);
    extern void __cxa_guard_release(void* pg) throw();
    extern void __cxa_guard_abort(void* pg) throw();
    extern void __cxa_guard_dummy(void);
}

const esp_elfsym cplusplus_symbols[] = {
    ESP_ELFSYM_EXPORT(_Znwj), // operator new(unsigned int)
    ESP_ELFSYM_EXPORT(_ZdlPvj), // operator delete(void*, unsigned int)
    // cxx_guards
    ESP_ELFSYM_EXPORT(__cxa_pure_virtual), // class-related, see https://arobenko.github.io/bare_metal_cpp/
    ESP_ELFSYM_EXPORT(__cxa_guard_acquire),
    ESP_ELFSYM_EXPORT(__cxa_guard_release),
    ESP_ELFSYM_EXPORT(__cxa_guard_abort),
    ESP_ELFSYM_EXPORT(__cxa_guard_dummy),
    // delimiter
    ESP_ELFSYM_END
};
