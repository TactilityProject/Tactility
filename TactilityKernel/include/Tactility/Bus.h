#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

/**
 * A bus contains a group of devices owned by a parent device.
 * It is used to make a group of devices discoverable.
 *
 * Example usage:
 *  The "i2c0" bus is created by the i2c_controller driver when a device is started by a driver.
 *  The postfix "0" means that it was the first device instance (at index 0).
 */
struct Bus {
    /** The name of the bus (e.g. "i2c0" or "spi1"). Valid characters: a-z a-Z 0-9 - _ . */
    const char* name;
    /** Internal data */
    struct {
        /** The device this bus belongs to */
        struct Device* parent_device;
        /** The bus' private data */
        void* data;
    } internal;
};

// region Bus management

int bus_construct(struct Bus* bus);

int bus_destruct(struct Bus* bus);

struct Bus* bus_find(const char* name);

// endregion

// region Bus device management

int bus_add_device(struct Bus*, struct Device* dev);

void bus_remove_device(struct Bus*, struct Device* dev);

// endregion

#ifdef __cplusplus
}
#endif
