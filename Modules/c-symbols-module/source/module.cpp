// SPDX-License-Identifier: Apache-2.0
#include <c_symbols/module.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <locale.h>

extern "C" {

#if CONFIG_IDF_TARGET_ESP32P4
// Newlib doesn't provide this (only Picolibc does); a compat shim is force-linked into the
// firmware for espressif__esp_ipa's sake - see
// Platforms/platform-esp32/source/drivers/esp32p4_libc_compat.c. Side-loaded apps built against
// Newlib need it resolved the same way.
extern int __issignalingf(float);
#endif

static const ModuleSymbol SYMBOLS[] = {
    // stdlib.h
    DEFINE_MODULE_SYMBOL(malloc),
    DEFINE_MODULE_SYMBOL(calloc),
    DEFINE_MODULE_SYMBOL(realloc),
    DEFINE_MODULE_SYMBOL(free),
    DEFINE_MODULE_SYMBOL(rand),
    DEFINE_MODULE_SYMBOL(srand),
    DEFINE_MODULE_SYMBOL(atof),
    DEFINE_MODULE_SYMBOL(atoi),
    DEFINE_MODULE_SYMBOL(atol),
    DEFINE_MODULE_SYMBOL(system),
    DEFINE_MODULE_SYMBOL(getenv),
    DEFINE_MODULE_SYMBOL(qsort),
    // time.h
    DEFINE_MODULE_SYMBOL(strftime),
    DEFINE_MODULE_SYMBOL(time),
    DEFINE_MODULE_SYMBOL(difftime),
    DEFINE_MODULE_SYMBOL(localtime),
    DEFINE_MODULE_SYMBOL(mktime),
    // math.h
    DEFINE_MODULE_SYMBOL(acoshf),
    DEFINE_MODULE_SYMBOL(acosf),
    DEFINE_MODULE_SYMBOL(asinhf),
    DEFINE_MODULE_SYMBOL(asinf),
    DEFINE_MODULE_SYMBOL(atanhf),
    DEFINE_MODULE_SYMBOL(atanf),
    DEFINE_MODULE_SYMBOL(atan2f),
    DEFINE_MODULE_SYMBOL(coshf),
    DEFINE_MODULE_SYMBOL(cosf),
    DEFINE_MODULE_SYMBOL(sinhf),
    DEFINE_MODULE_SYMBOL(sinf),
    DEFINE_MODULE_SYMBOL(tanhf),
    DEFINE_MODULE_SYMBOL(tanf),
    DEFINE_MODULE_SYMBOL(expf),
    DEFINE_MODULE_SYMBOL(ldexpf),
    DEFINE_MODULE_SYMBOL(logf),
    DEFINE_MODULE_SYMBOL(log10f),
    DEFINE_MODULE_SYMBOL(powf),
    DEFINE_MODULE_SYMBOL(sqrtf),
    DEFINE_MODULE_SYMBOL(fmodf),
    DEFINE_MODULE_SYMBOL(frexpf),
    DEFINE_MODULE_SYMBOL(modff),
    DEFINE_MODULE_SYMBOL(ceilf),
    DEFINE_MODULE_SYMBOL(fabsf),
    DEFINE_MODULE_SYMBOL(floorf),
    DEFINE_MODULE_SYMBOL(fmaxf),
    DEFINE_MODULE_SYMBOL(fminf),
    DEFINE_MODULE_SYMBOL(roundf),
#if CONFIG_IDF_TARGET_ESP32P4
    DEFINE_MODULE_SYMBOL(__issignalingf),
#endif
    // Explicit signatures: libstdc++/libc++'s <cmath> float/double/long double overloads make a
    // bare `&acos` etc. ambiguous. esp-idf newlib's plain, unoverloaded functions match these signatures
    // exactly, so the cast is a no-op there - one list works for all platforms:
    DEFINE_MODULE_SYMBOL_SIGNATURE(acos, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(asin, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(atan, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(atan2, double (*)(double, double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(cos, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(sin, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(sinh, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(tan, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(exp, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(ldexp, double (*)(double, int)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(log, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(log10, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(pow, double (*)(double, double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(sqrt, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(fmod, double (*)(double, double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(frexp, double (*)(double, int*)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(modf, double (*)(double, double*)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(ceil, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(fabs, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(floor, double (*)(double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(fmax, double (*)(double, double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(fmin, double (*)(double, double)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(round, double (*)(double)),
    // cassert / cstdio
    DEFINE_MODULE_SYMBOL(abort),
    DEFINE_MODULE_SYMBOL(fclose),
    DEFINE_MODULE_SYMBOL(feof),
    DEFINE_MODULE_SYMBOL(ferror),
    DEFINE_MODULE_SYMBOL(fflush),
    DEFINE_MODULE_SYMBOL(fgetc),
    DEFINE_MODULE_SYMBOL(fgetpos),
    DEFINE_MODULE_SYMBOL(fgets),
    DEFINE_MODULE_SYMBOL(fopen),
    DEFINE_MODULE_SYMBOL(freopen),
    DEFINE_MODULE_SYMBOL(setvbuf),
    DEFINE_MODULE_SYMBOL(fputc),
    DEFINE_MODULE_SYMBOL(fputs),
    DEFINE_MODULE_SYMBOL(fprintf),
    DEFINE_MODULE_SYMBOL(fread),
    DEFINE_MODULE_SYMBOL(fseek),
    DEFINE_MODULE_SYMBOL(fsetpos),
    DEFINE_MODULE_SYMBOL(fscanf),
    DEFINE_MODULE_SYMBOL(ftell),
    DEFINE_MODULE_SYMBOL(fwrite),
    DEFINE_MODULE_SYMBOL(getc),
    DEFINE_MODULE_SYMBOL(putc),
    DEFINE_MODULE_SYMBOL(putchar),
    DEFINE_MODULE_SYMBOL(puts),
    DEFINE_MODULE_SYMBOL(printf),
    DEFINE_MODULE_SYMBOL(sscanf),
    DEFINE_MODULE_SYMBOL(snprintf),
    DEFINE_MODULE_SYMBOL(sprintf),
    DEFINE_MODULE_SYMBOL(vsprintf),
    DEFINE_MODULE_SYMBOL(vsnprintf),
    DEFINE_MODULE_SYMBOL(vfprintf),
    DEFINE_MODULE_SYMBOL(rename),
    DEFINE_MODULE_SYMBOL(rewind),
    DEFINE_MODULE_SYMBOL(remove),
    // cstring
    DEFINE_MODULE_SYMBOL(strlen),
    DEFINE_MODULE_SYMBOL(strcmp),
    DEFINE_MODULE_SYMBOL(strncmp),
    DEFINE_MODULE_SYMBOL(strncpy),
    DEFINE_MODULE_SYMBOL(strcpy),
    DEFINE_MODULE_SYMBOL(strcat),
    // Explicit signatures due to libstdc++'s <cstring> C++ overloads on some platforms
    DEFINE_MODULE_SYMBOL_SIGNATURE(strchr, const char* (*)(const char*, int)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(strstr, const char* (*)(const char*, const char*)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(strrchr, const char* (*)(const char*, int)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(strpbrk, const char* (*)(const char*, const char*)),
    DEFINE_MODULE_SYMBOL_SIGNATURE(memchr, const void* (*)(const void*, int, size_t)),
    DEFINE_MODULE_SYMBOL(strerror),
    DEFINE_MODULE_SYMBOL(strtod),
    DEFINE_MODULE_SYMBOL(strtol),
    DEFINE_MODULE_SYMBOL(strtoul),
    DEFINE_MODULE_SYMBOL(strcspn),
    DEFINE_MODULE_SYMBOL(strncat),
    DEFINE_MODULE_SYMBOL(strspn),
    DEFINE_MODULE_SYMBOL(strcoll),
    DEFINE_MODULE_SYMBOL(memset),
    DEFINE_MODULE_SYMBOL(memcpy),
    DEFINE_MODULE_SYMBOL(memcmp),
    DEFINE_MODULE_SYMBOL(memmove),
    // ctype.h
#ifdef ESP_PLATFORM
    // _ctype_ is newlib's internal lookup table backing isalnum() etc.;
    // glibc/macOS libc don't export a symbol by this name.
    DEFINE_MODULE_SYMBOL(_ctype_),
#endif
    DEFINE_MODULE_SYMBOL(isalnum),
    DEFINE_MODULE_SYMBOL(isalpha),
    DEFINE_MODULE_SYMBOL(iscntrl),
    DEFINE_MODULE_SYMBOL(isdigit),
    DEFINE_MODULE_SYMBOL(isgraph),
    DEFINE_MODULE_SYMBOL(islower),
    DEFINE_MODULE_SYMBOL(isprint),
    DEFINE_MODULE_SYMBOL(ispunct),
    DEFINE_MODULE_SYMBOL(isspace),
    DEFINE_MODULE_SYMBOL(isupper),
    DEFINE_MODULE_SYMBOL(isxdigit),
    DEFINE_MODULE_SYMBOL(tolower),
    DEFINE_MODULE_SYMBOL(toupper),
    // locale.h
    DEFINE_MODULE_SYMBOL(localeconv),
    MODULE_SYMBOL_TERMINATOR
};

Module c_symbols_module = {
    .name = "c-symbols",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
