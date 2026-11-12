// SPDX-License-Identifier: Apache-2.0
#include <development/remote_input.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/touch.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <cstring>
#include <new>

namespace {

constexpr auto* TAG = "DevelopmentRemoteInput";

constexpr size_t KEY_QUEUE_CAPACITY = 32;
constexpr MemoryPolicy MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

}

struct RemoteInput {
    Mutex mutex {};
    uint8_t enabled = 0;

    Device keyboard {};
    Device touch {};

    KeyboardKeyData keys[KEY_QUEUE_CAPACITY] {};
    size_t keyHead = 0;
    size_t keyCount = 0;

    RemoteTouchPoint touchPoints[REMOTE_INPUT_MAX_TOUCH_POINTS] {};
    uint8_t touchCount = 0;
};

namespace {

RemoteInput* inputOf(Device* device) {
    return static_cast<RemoteInput*>(device_get_driver_data(device));
}

// region Keyboard

error_t keyboardReadKey(Device* device, KeyboardKeyData* data) {
    auto* input = inputOf(device);
    mutex_lock(&input->mutex);
    if (input->keyCount == 0) {
        *data = {};
    } else {
        *data = input->keys[input->keyHead];
        input->keyHead = (input->keyHead + 1) % KEY_QUEUE_CAPACITY;
        input->keyCount--;
        data->continue_reading = input->keyCount > 0;
    }
    mutex_unlock(&input->mutex);
    return ERROR_NONE;
}

const KeyboardApi keyboardApi = {
    .capabilities = 0,
    .read_key = keyboardReadKey,
    .get_backlight = nullptr,
};

Driver keyboardDriver = {
    .name = "development_remote_keyboard",
    .compatible = (const char*[]) { "tactility,development-remote-keyboard", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &keyboardApi,
    .device_type = &KEYBOARD_TYPE,
    .internal = nullptr,
};

// endregion

// region Touch

error_t touchReadData(Device*, TickType_t) {
    return ERROR_NONE;
}

bool touchGetTouchedPoints(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* pointCount, uint8_t maxPointCount) {
    auto* input = inputOf(device);
    mutex_lock(&input->mutex);
    const uint8_t count = input->touchCount < maxPointCount ? input->touchCount : maxPointCount;
    for (uint8_t i = 0; i < count; i++) {
        x[i] = input->touchPoints[i].x;
        y[i] = input->touchPoints[i].y;
        if (strength != nullptr) {
            strength[i] = 1;
        }
    }
    *pointCount = count;
    mutex_unlock(&input->mutex);
    return count > 0;
}

// The viewer already sends coordinates in the orientation of the image it shows
error_t touchSetAxis(Device*, bool) {
    return ERROR_NONE;
}

error_t touchGetAxis(Device*, bool* value) {
    *value = false;
    return ERROR_NONE;
}

const TouchApi touchApi = {
    .enter_sleep = nullptr,
    .exit_sleep = nullptr,
    .read_data = touchReadData,
    .get_touched_points = touchGetTouchedPoints,
    .set_swap_xy = touchSetAxis,
    .get_swap_xy = touchGetAxis,
    .set_mirror_x = touchSetAxis,
    .get_mirror_x = touchGetAxis,
    .set_mirror_y = touchSetAxis,
    .get_mirror_y = touchGetAxis,
};

Driver touchDriver = {
    .name = "development_remote_touch",
    .compatible = (const char*[]) { "tactility,development-remote-touch", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &touchApi,
    .device_type = &TOUCH_TYPE,
    .internal = nullptr,
};

// endregion

bool createDevice(RemoteInput* input, Device* device, const char* name, Driver* driver) {
    *device = Device {};
    device->name = name;
    if (device_construct(device) != ERROR_NONE) {
        return false;
    }
    device_set_driver_data(device, input);
    device_set_driver(device, driver);
    device->flags |= DEVICE_FLAG_VIRTUAL | DEVICE_FLAG_DYNAMIC;
    if (device_add(device) != ERROR_NONE) {
        device_destruct(device);
        return false;
    }
    if (device_start(device) != ERROR_NONE) {
        device_remove(device);
        device_destruct(device);
        return false;
    }
    return true;
}

void destroyDevice(Device* device) {
    device_stop(device);
    device_remove(device);
    // Other tasks can still hold a reference for a short while after the device stopped
    for (int attempt = 0; attempt < 100; attempt++) {
        const error_t error = device_destruct(device);
        if (error != ERROR_RESOURCE_BUSY) {
            if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to destruct %s", device->name);
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    LOG_E(TAG, "Device %s is still in use", device->name);
}

struct Kind {
    uint8_t bit;
    Device RemoteInput::* device;
    const char* name;
    Driver* driver;
};

const Kind KINDS[] = {
    { DEVELOPMENT_REMOTE_INPUT_KEYBOARD, &RemoteInput::keyboard, "remote_keyboard0", &keyboardDriver },
    { DEVELOPMENT_REMOTE_INPUT_TOUCH, &RemoteInput::touch, "remote_touch0", &touchDriver },
};

} // namespace

RemoteInput* remote_input_alloc() {
    auto* input = static_cast<RemoteInput*>(memory_alloc_with_policy(sizeof(RemoteInput), &MEMORY_POLICY));
    if (input == nullptr) {
        return nullptr;
    }
    new (input) RemoteInput();
    mutex_construct(&input->mutex);

    for (const Kind& kind : KINDS) {
        if (driver_construct_add(kind.driver) != ERROR_NONE) {
            LOG_E(TAG, "Failed to add %s", kind.driver->name);
            for (const Kind& added : KINDS) {
                if (&added == &kind) {
                    break;
                }
                driver_remove_destruct(added.driver);
            }
            mutex_destruct(&input->mutex);
            memory_free(input);
            return nullptr;
        }
    }
    return input;
}

void remote_input_free(RemoteInput* input) {
    remote_input_set_enabled(input, 0);
    for (const Kind& kind : KINDS) {
        driver_remove_destruct(kind.driver);
    }
    mutex_destruct(&input->mutex);
    memory_free(input);
}

bool remote_input_set_enabled(RemoteInput* input, uint8_t mask) {
    bool success = true;
    for (const Kind& kind : KINDS) {
        const bool wanted = (mask & kind.bit) != 0;
        const bool present = (input->enabled & kind.bit) != 0;
        Device* device = &(input->*kind.device);
        if (wanted && !present) {
            if (createDevice(input, device, kind.name, kind.driver)) {
                input->enabled |= kind.bit;
            } else {
                LOG_E(TAG, "Failed to create %s", kind.name);
                success = false;
            }
        } else if (!wanted && present) {
            // Cleared first so that stale state doesn't leak into the next session
            mutex_lock(&input->mutex);
            input->keyHead = 0;
            input->keyCount = 0;
            input->touchCount = 0;
            mutex_unlock(&input->mutex);
            destroyDevice(device);
            input->enabled &= ~kind.bit;
        }
    }
    return success;
}

void remote_input_key(RemoteInput* input, uint32_t key, bool pressed, bool ctrl, bool alt) {
    mutex_lock(&input->mutex);
    if ((input->enabled & DEVELOPMENT_REMOTE_INPUT_KEYBOARD) != 0 && input->keyCount < KEY_QUEUE_CAPACITY) {
        KeyboardKeyData data {};
        data.key = key;
        data.pressed = pressed;
        data.ctrl = ctrl;
        data.alt = alt;
        input->keys[(input->keyHead + input->keyCount) % KEY_QUEUE_CAPACITY] = data;
        input->keyCount++;
    }
    mutex_unlock(&input->mutex);
}

void remote_input_touch(RemoteInput* input, const RemoteTouchPoint* points, uint8_t count) {
    mutex_lock(&input->mutex);
    if ((input->enabled & DEVELOPMENT_REMOTE_INPUT_TOUCH) != 0) {
        input->touchCount = count < REMOTE_INPUT_MAX_TOUCH_POINTS ? count : REMOTE_INPUT_MAX_TOUCH_POINTS;
        for (uint8_t i = 0; i < input->touchCount; i++) {
            input->touchPoints[i] = points[i];
        }
    }
    mutex_unlock(&input->mutex);
}
