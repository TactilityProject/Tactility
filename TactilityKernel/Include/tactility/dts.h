#pragma once

struct Device;

/** Signals the intended state of a device. */
enum DtsDeviceStatus {
    /** Device should be constructed, added and started. */
    DTS_DEVICE_STATUS_OKAY,
    /** Device should be constructed and added, but not started. */
    DTS_DEVICE_STATUS_DISABLED
};

/**
 * Holds a device pointer and a compatible string.
 * The device must not be constructed, added or started yet.
 * This is used by the devicetree code generator and the application init sequence.
 */
struct DtsDevice {
    /** A pointer to a device. */
    struct Device* device;
    /** The compatible string contains the identifier of the driver that this device is compatible with. */
    const char* compatible;
    /** The intended state of the device. */
    const enum DtsDeviceStatus status;
};

/** Signals the end of the device array in the generated dts code. */
#define DTS_DEVICE_TERMINATOR { NULL, NULL, DTS_DEVICE_STATUS_DISABLED }
