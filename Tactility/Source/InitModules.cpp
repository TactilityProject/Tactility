#include <Tactility/TactilityPrivate.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <app_esp32/module.h>
#endif

#if __has_include(<unistd.h>) && not defined(ESP_PLATFORM)
#define TT_IS_POSIX 1
#else
#define TT_IS_POSIX 0
#endif

#if TT_IS_POSIX or defined(ESP_PLATFORM) // esp-idf supports certain posix symbols
#include <posix_symbols/module.h>
#endif

#if TT_IS_POSIX
#include <app_posix/module.h>
#endif

#include <app/module.h>
#include <audio_decoder/module.h>
#include <binfont/module.h>
#include <c_symbols/module.h>
#include <cjson_symbols/module.h>
#include <coreutils/module.h>
#include <cpp_symbols/module.h>
#include <crypt/module.h>
#include <freertos/module.h>
#include <gps/module.h>
#include <gps_generic/module.h>
#include <gps_meshtastic/module.h>
#include <graphics/module.h>
#include <http/module.h>
#include <lodepng/module.h>
#include <mbedtls/module.h>
#include <pthread/module.h>

#include <tactility/check.h>
#include <tactility/module.h>

// Audio service exports to external ELF apps (Source/service/audio/AudioExports.cpp).
extern "C" Module tactility_audio_module;

namespace tt {

void initModules() {
    // The following groups of symbols are sorted by the estimated chance of them occurring

    // C/C++/Posix symbols
    check(module_ensure_started(&c_symbols_module) == ERROR_NONE);
    check(module_ensure_started(&cjson_module) == ERROR_NONE);
#if TT_IS_POSIX or defined(ESP_PLATFORM) // esp-idf supports certain posix symbols
    check(module_ensure_started(&posix_symbols_module) == ERROR_NONE);
#endif
    check(module_ensure_started(&cpp_symbols_module) == ERROR_NONE);
    // OS level symbols
    check(module_ensure_started(&freertos_module) == ERROR_NONE);
    check(module_ensure_started(&pthread_module) == ERROR_NONE);
    // Other libraries
    check(module_ensure_started(&http_module) == ERROR_NONE);
    check(module_ensure_started(&graphics_module) == ERROR_NONE);
    check(module_ensure_started(&lodepng_module) == ERROR_NONE);
    check(module_ensure_started(&binfont_module) == ERROR_NONE);
    check(module_ensure_started(&app_module) == ERROR_NONE);
    check(module_ensure_started(&coreutils_module) == ERROR_NONE);
    check(module_ensure_started(&crypt_module) == ERROR_NONE);
    check(module_ensure_started(&audio_decoder_module) == ERROR_NONE);
    check(module_ensure_started(&mbedtls_module) == ERROR_NONE);
    check(module_ensure_started(&gps_module) == ERROR_NONE);
    check(module_ensure_started(&gps_generic_module) == ERROR_NONE);
    check(module_ensure_started(&gps_meshtastic_module) == ERROR_NONE);
#ifdef ESP_PLATFORM
    check(module_ensure_started(&app_esp32_module) == ERROR_NONE);
#elif TT_IS_POSIX
    check(module_ensure_started(&app_posix_module) == ERROR_NONE);
#endif
    check(module_ensure_started(&tactility_audio_module) == ERROR_NONE);
}

} // namespace
