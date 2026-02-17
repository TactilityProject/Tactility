#pragma once

#include <tactility/dts.h>
#include <tactility/error.h>
#include <tactility/module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the kernel with platform and device modules, and a device tree.
 * @param dts_modules List of modules from devicetree, null-terminated, non-null parameter
 * @param dts_devices The list of generated devices from the devicetree. The array must be terminated with DTS_DEVICE_TERMINATOR. This parameter can be NULL.
 * @return ERROR_NONE on success, otherwise an error code
 */
error_t kernel_init(struct Module* dts_modules[], struct DtsDevice dts_devices[]);

#ifdef __cplusplus
}
#endif
