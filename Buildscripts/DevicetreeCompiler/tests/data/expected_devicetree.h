#pragma once
#include <tactility/error.h>
#include <tactility/dts.h>

#ifdef __cplusplus
extern "C" {
#endif

// Array of device tree modules terminated with DTS_MODULE_TERMINATOR
extern struct DtsDevice dts_devices[];

// Array of module symbols terminated with NULL
extern struct Module* dts_modules[];

#ifdef __cplusplus
}
#endif
