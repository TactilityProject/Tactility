#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

// ---- HID device mode ----

/**
 * Selects the HID report descriptor and appearance used when this device
 * operates as a BLE HID peripheral.
 */
enum BtHidDeviceMode {
    /** Keyboard (report ID 1, 8 bytes) + Consumer (report ID 2, 2 bytes). */
    BT_HID_DEVICE_MODE_KEYBOARD,
    /** Mouse only (report ID 1, 4 bytes). */
    BT_HID_DEVICE_MODE_MOUSE,
    /** Keyboard + Consumer + Mouse (report IDs 1, 2, 3). */
    BT_HID_DEVICE_MODE_KEYBOARD_MOUSE,
    /** Gamepad (report ID 1, 8 bytes: 2-byte buttons + 6-byte axes). */
    BT_HID_DEVICE_MODE_GAMEPAD,
};

// ---- BLE HID Device child device ----

/**
 * BLE HID device profile API (present this device as a BLE HID peripheral).
 * This API is exposed by a child device of the Bluetooth device.
 */
struct BtHidDeviceApi {
    /**
     * Start advertising as a BLE HID device with the given mode.
     * Sets up the appropriate GATT report descriptor and starts advertising.
     * @param[in] device the HID device child device
     * @param[in] mode   the HID device mode (keyboard, mouse, gamepad, etc.)
     * @return ERROR_NONE on success
     */
    error_t (*start)(struct Device* device, enum BtHidDeviceMode mode);

    /**
     * Stop advertising as a BLE HID device and close any active connection.
     * @param[in] device the HID device child device
     * @return ERROR_NONE on success
     */
    error_t (*stop)(struct Device* device);

    /**
     * Send a single key event when operating as a HID keyboard device.
     * @param[in] device  the HID device child device
     * @param[in] keycode the HID keycode
     * @param[in] pressed true for key down, false for key up
     * @return ERROR_NONE on success
     */
    error_t (*send_key)(struct Device* device, uint8_t keycode, bool pressed);

    /**
     * Send a full keyboard HID report (8 bytes: modifier, reserved, keycodes[6]).
     * @param[in] device the HID device child device
     * @param[in] report pointer to the 8-byte keyboard report
     * @param[in] len    number of bytes (up to 8)
     * @return ERROR_NONE on success
     */
    error_t (*send_keyboard)(struct Device* device, const uint8_t* report, size_t len);

    /**
     * Send a consumer control HID report (2 bytes: 16-bit usage code, little-endian).
     * @param[in] device the HID device child device
     * @param[in] report pointer to the 2-byte consumer report
     * @param[in] len    number of bytes (up to 2)
     * @return ERROR_NONE on success
     */
    error_t (*send_consumer)(struct Device* device, const uint8_t* report, size_t len);

    /**
     * Send a mouse HID report (4 bytes: buttons, X, Y, wheel).
     * @param[in] device the HID device child device
     * @param[in] report pointer to the 4-byte mouse report
     * @param[in] len    number of bytes (up to 4)
     * @return ERROR_NONE on success
     */
    error_t (*send_mouse)(struct Device* device, const uint8_t* report, size_t len);

    /**
     * Send a gamepad HID report (8 bytes: buttons[2] + axes[6]).
     * @param[in] device the HID device child device
     * @param[in] report pointer to the 8-byte gamepad report
     * @param[in] len    number of bytes (up to 8)
     * @return ERROR_NONE on success
     */
    error_t (*send_gamepad)(struct Device* device, const uint8_t* report, size_t len);

    /**
     * @param[in] device the HID device child device
     * @return true when a remote host is connected to this HID device
     */
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType BLUETOOTH_HID_DEVICE_TYPE;

/** Find the first ready BLE HID device child device. Returns NULL if unavailable. */
struct Device* bluetooth_hid_device_get_device(void);

error_t bluetooth_hid_device_start(struct Device* device, enum BtHidDeviceMode mode);
error_t bluetooth_hid_device_stop(struct Device* device);
error_t bluetooth_hid_device_send_key(struct Device* device, uint8_t keycode, bool pressed);
error_t bluetooth_hid_device_send_keyboard(struct Device* device, const uint8_t* report, size_t len);
error_t bluetooth_hid_device_send_consumer(struct Device* device, const uint8_t* report, size_t len);
error_t bluetooth_hid_device_send_mouse(struct Device* device, const uint8_t* report, size_t len);
error_t bluetooth_hid_device_send_gamepad(struct Device* device, const uint8_t* report, size_t len);
bool    bluetooth_hid_device_is_connected(struct Device* device);

#ifdef __cplusplus
}
#endif
