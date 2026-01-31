#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVICE_TYPE_I2C,
    DEVICE_TYPE_DISPLAY,
    DEVICE_TYPE_TOUCH,
    DEVICE_TYPE_SDCARD,
    DEVICE_TYPE_KEYBOARD,
    DEVICE_TYPE_POWER,
    DEVICE_TYPE_GPS
} TtDeviceType;

typedef uint32_t DeviceId;

/**
 * Find one or more devices of a certain type.
 * @param[in] type the type to look for
 * @param[inout] deviceIds the output ids, which should fit at least maxCount amount of devices
 * @param[out] count the resulting number of device ids that were returned
 * @param[in] maxCount the maximum number of items that the "deviceIds" output can contain (minimum value is 1)
 * @return true if one or more devices were found
 */
bool tt_hal_device_find(TtDeviceType type, DeviceId* deviceIds, uint16_t* count, uint16_t maxCount);

#ifdef __cplusplus
}
#endif
