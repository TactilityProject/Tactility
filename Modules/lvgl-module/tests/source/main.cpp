#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <cstdlib>

// The kernel code that grid navigation links to needs it, and the platform module that normally provides it isn't loaded
extern "C" void vAssertCalled(unsigned long line, const char* const file) {
    std::fprintf(stderr, "assert failed at %s:%lu\n", file, line);
    std::abort();
}
