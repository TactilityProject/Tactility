#pragma once

#include <tactility/error.h>

struct Device;
struct Driver;

/**
 * Runs a function on the USB host worker task and blocks until it returns.
 * Use it for device_start() and device_stop() of dynamic devices, so device listeners never run on a USB task.
 * The worker task is created on demand, so this can fail when memory is low.
 * @param[in] host the USB host device
 * @param[in] function the function to run
 * @param[in] context passed to the function
 * @retval ERROR_NONE when the function ran
 * @retval ERROR_INVALID_STATE when the host is not started
 * @retval ERROR_RESOURCE when the worker task could not be created
 */
error_t esp32_usbhost_run_on_worker(struct Device* host, void (*function)(void* context), void* context);

/**
 * Constructs, adds and starts a device on the worker task.
 * The driver data is set before the device starts.
 * @param[in] host the USB host device
 * @param[in,out] device storage that must outlive a successful esp32_usbhost_device_destroy()
 * @param[in] parent the parent of the new device
 * @retval ERROR_NONE when the device started
 */
error_t esp32_usbhost_device_create(struct Device* host, struct Device* device, struct Device* parent, const char* name, struct Driver* driver, void* driver_data);

/**
 * Stops, removes and destructs a device on the worker task.
 * Waits a while for references held by other tasks to be released.
 * @param[in] host the USB host device
 * @param[in,out] device a device created with esp32_usbhost_device_create()
 * @retval ERROR_NONE when the device was destructed
 * @retval ERROR_RESOURCE_BUSY when references are still held. The device storage is still in use and calling this again retries.
 */
error_t esp32_usbhost_device_destroy(struct Device* host, struct Device* device);
