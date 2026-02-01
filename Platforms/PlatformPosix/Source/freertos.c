#include <stdlib.h>
#if defined(__GLIBC__)
#include <assert.h>
#endif
#include <tactility/freertos/task.h>
#include <tactility/log.h>

#define TAG LOG_TAG(freertos)

/**
 * Assert implementation as defined in the FreeRTOSConfig.h
 */
void vAssertCalled(unsigned long line, const char* const file) {
    LOG_E("Assert triggered at {}:{}", file, line);
#if defined(__GLIBC__)
    __assert_fail("assert failed", file, line, "");
#else
    abort();
#endif
}
