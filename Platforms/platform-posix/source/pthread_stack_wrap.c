#include <pthread.h>
#include <stddef.h>

/**
 * FreeRTOS's POSIX port hands every task a stack carved from its own heap (pvPortMalloc) via this call.
 * pthread_attr_setstack requires page alignment that pvPortMalloc doesn't guarantee.
 * When it succeeds anyway, the task's real pthread stack ends up living inside that small FreeRTOS heap region,
 * where a thread doing heavier stack work (e.g. Mesa GL shader compilation) can silently corrupt adjacent heap_4 objects.
 * No-op'ing the call instead leaves every task's pthread_attr_t at its pthread_attr_init() default,
 * so pthread_create() gives it a real, properly sized stack.
 *
 * Linked via -Wl,--wrap=pthread_attr_setstack (Tactility/CMakeLists.txt) on non-Apple platforms;
 * reused as a dyld interpose target below on Apple platforms, whose linker lacks --wrap.
 */
int __wrap_pthread_attr_setstack(pthread_attr_t* attr, void* stackaddr, size_t stacksize) {
    (void)attr;
    (void)stackaddr;
    (void)stacksize;
    return 0;
}

#ifdef __APPLE__
#include <mach-o/dyld-interposing.h>

DYLD_INTERPOSE(__wrap_pthread_attr_setstack, pthread_attr_setstack)

#endif
