#include <stdio.h>
#include <stdlib.h>
#if defined(__GLIBC__)
#include <assert.h>
#endif
#include <tactility/freertos/task.h>

/**
 * Assert implementation as defined in the FreeRTOSConfig.h
 */
void vAssertCalled(unsigned long line, const char* const file) {
    // Logging can recurse into this assert when it happens from inside the log queue
    fprintf(stderr, "Assert triggered at %s:%lu\n", file, line);
#if defined(__GLIBC__)
    __assert_fail("assert failed", file, line, "");
#else
    abort();
#endif
}
