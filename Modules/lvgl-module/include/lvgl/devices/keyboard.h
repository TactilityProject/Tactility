// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct LvglSoftwareKeyboard {
    lv_obj_t* object;
};

/**
 * @brief Creates an lv_indev_t bound to the given KEYBOARD_TYPE device and registers a read callback
 * that polls the device through its KeyboardApi.
 *
 * @warning Caller must hold the LVGL lock (see lvgl_lock() in lvgl_module.h) — call this from
 * LvglModuleConfig.on_start, or after calling lvgl_lock() explicitly.
 *
 * @note Idempotent per device: if an indev is already bound to this device (see
 * lvgl_keyboard_find_by_device()), that indev is returned instead of creating a second one.
 *
 * @param[in] device a device of type KEYBOARD_TYPE
 * @param[in] display the display this indev should be associated with, or NULL to leave it unset
 * @param[out] out_indev the created (or already-existing) indev, valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device or out_indev is NULL, or device is not of type KEYBOARD_TYPE
 * @retval ERROR_OUT_OF_MEMORY if allocation failed
 */
error_t lvgl_keyboard_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev);

/**
 * @brief Removes an indev previously created with lvgl_keyboard_add().
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_keyboard_remove(lv_indev_t* indev);

/**
 * @brief Finds the indev previously created with lvgl_keyboard_add() for the given device, if any.
 * @warning Caller must hold the LVGL lock.
 * @param[in] device a device of type KEYBOARD_TYPE
 * @return the bound indev, or NULL if none is bound to this device
 */
lv_indev_t* lvgl_keyboard_find_by_device(struct Device* device);

/**
 * @brief Assigns the indev to the active keyboard input group, so it can drive focus
 * navigation and input for focused widgets.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_keyboard_enable(lv_indev_t* indev);

/**
 * @brief Detaches the indev from the shared keyboard input group.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_keyboard_disable(lv_indev_t* indev);

/**
 * @brief Makes @a group the group that receives keypad input: it becomes the default group (new
 * widgets join it), and every keypad/encoder indev that is attached to a group is moved to it.
 * Indevs enabled later are attached to it too.
 * @warning Caller must hold the LVGL lock.
 * @param[in] group the group to activate, or NULL to activate the shared keyboard input group
 */
void lvgl_keyboard_set_active_group(lv_group_t* group);

/**
 * @return the shared keyboard input group, which is the default when no other group is active
 */
lv_group_t* lvgl_keyboard_get_app_group(void);

/**
 * @brief Adds the textarea to the shared keyboard navigation group (so any keypad indev -
 * hardware or on-screen - can type into it once it's focused), and, only when
 * lvgl_software_keyboard_is_enabled() is true (i.e. no hardware keyboard is present), wires it
 * up to show/hide the on-screen keyboard on focus/defocus/ready.
 * @warning Caller must hold the LVGL lock.
 * @param[in] keyboard the on-screen keyboard to associate with the textarea
 * @param[in] textarea the lv_textarea_t object to wire up
 */
void lvgl_keyboard_add_textarea(struct LvglSoftwareKeyboard* keyboard, lv_obj_t* textarea);

/**
 * @brief Checks whether a ready (started) KEYBOARD_TYPE kernel device is present.
 * @return true if a hardware keyboard device is available
 */
bool lvgl_hardware_keyboard_is_available();

/**
 * @brief Assigns the shared keyboard navigation group to a keypad indev that wasn't created via
 * lvgl_keyboard_add() (e.g. a USB HID keyboard managed outside the kernel device system).
 *
 * @warning Caller must hold the LVGL lock. Requires the keyboard navigation group to already
 * exist (created during LVGL module start).
 * @param[in] device the keypad indev to attach to the navigation group
 */
void lvgl_hardware_keyboard_add_custom(lv_indev_t* device);

/**
 * @brief Detaches an indev previously registered with lvgl_hardware_keyboard_add_custom() and
 * frees its associated context.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_hardware_keyboard_remove_custom(lv_indev_t* device);

/**
 * @brief Creates the on-screen keyboard widget as a hidden child of parent, and remembers it as
 * the last constructed software keyboard (see lvgl_software_keyboard_get_last()).
 * @warning Caller must hold the LVGL lock.
 * @param[out] keyboard the software keyboard struct to initialize
 * @param[in] parent the lv_obj_t that will own the keyboard widget
 */
void lvgl_software_keyboard_construct(struct LvglSoftwareKeyboard* keyboard, lv_obj_t* parent);

/**
 * @brief Deletes the on-screen keyboard widget created by lvgl_software_keyboard_construct().
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_software_keyboard_destruct(struct LvglSoftwareKeyboard* keyboard);

/**
 * @brief Unhides the on-screen keyboard and binds it to the given textarea for input.
 * @warning Caller must hold the LVGL lock.
 * @param[in] keyboard the software keyboard to show
 * @param[in] textarea the lv_textarea_t that receives the keyboard's input
 */
void lvgl_software_keyboard_show(struct LvglSoftwareKeyboard* keyboard, lv_obj_t* textarea);

/**
 * @brief Hides the on-screen keyboard.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_software_keyboard_hide(struct LvglSoftwareKeyboard* keyboard);

/**
 * The on-screen keyboard is only shown when there is no hardware keyboard driver active.
 * @return if we should show a on-screen keyboard for text input inside our apps
 */
bool lvgl_software_keyboard_is_enabled();

/**
 * @return the most recently constructed software keyboard, or one with a NULL object if none
 * has been constructed yet (or the last one was destructed)
 */
struct LvglSoftwareKeyboard* lvgl_software_keyboard_get_last();

/**
 * @brief Attaches the active keyboard navigation group to every currently registered keypad
 * indev, so they can be used to navigate the on-screen keyboard and focused widgets.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_software_keyboard_activate(struct LvglSoftwareKeyboard* keyboard);

/**
 * @brief Detaches the navigation group from every currently registered keypad indev (inverse of
 * lvgl_software_keyboard_activate()).
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_software_keyboard_deactivate(struct LvglSoftwareKeyboard* keyboard);

#ifdef __cplusplus
}
#endif
