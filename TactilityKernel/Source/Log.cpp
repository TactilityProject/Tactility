// SPDX-License-Identifier: Apache-2.0

#ifndef ESP_PLATFORM

#include <Tactility/Log.h>

#include <stdio.h>
#include <stdarg.h>

extern "C" {

void log_generic(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    printf("%s ", tag);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

}

#endif