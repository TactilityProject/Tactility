// SPDX-License-Identifier: Apache-2.0
#include "unphone_nav_buttons.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <Tactility/Thread.h>
#include <Tactility/app/App.h>
#include <Tactility/kernel/Kernel.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <new>

#define TAG "UnphoneNavButtons"
#define GET_CONFIG(device) (static_cast<const UnphoneNavButtonsConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<UnphoneNavButtonsInternal*>(device_get_driver_data(device)))

namespace {
// Only button 1 currently drives any behavior (stopping the running app); 2 and 3 are
// read and queued like button 1 but nothing consumes them yet (mirrors the original
// unPhone nav button behavior).
constexpr int BUTTON1_INDEX = 1;
constexpr int BUTTON2_INDEX = 2;
constexpr int BUTTON3_INDEX = 3;
}

struct UnphoneNavButtonsInternal {
    GpioDescriptor* pin_button1 = nullptr;
    GpioDescriptor* pin_button2 = nullptr;
    GpioDescriptor* pin_button3 = nullptr;

    QueueHandle_t event_queue = nullptr;
    tt::Thread thread;
    bool thread_interrupt_requested = false;
};

// region ISR callbacks

static void IRAM_ATTR on_button1(void* arg) {
    auto* internal = static_cast<UnphoneNavButtonsInternal*>(arg);
    int index = BUTTON1_INDEX;
    xQueueSendFromISR(internal->event_queue, &index, nullptr);
}

static void IRAM_ATTR on_button2(void* arg) {
    auto* internal = static_cast<UnphoneNavButtonsInternal*>(arg);
    int index = BUTTON2_INDEX;
    xQueueSendFromISR(internal->event_queue, &index, nullptr);
}

static void IRAM_ATTR on_button3(void* arg) {
    auto* internal = static_cast<UnphoneNavButtonsInternal*>(arg);
    int index = BUTTON3_INDEX;
    xQueueSendFromISR(internal->event_queue, &index, nullptr);
}

// endregion

// region Consumer thread

static int32_t nav_buttons_thread_main(UnphoneNavButtonsInternal* internal) {
    int button_index;
    while (!internal->thread_interrupt_requested) {
        if (xQueueReceive(internal->event_queue, &button_index, portMAX_DELAY)) {
            // The buttons might generate more than 1 click because of how they are built
            LOG_I(TAG, "Pressed button %d", button_index);
            if (button_index == BUTTON1_INDEX) {
                tt::app::stop();
            }

            // Debounce all events for a short period of time
            // This is easier than keeping track when each button was last pressed
            tt::kernel::delayMillis(50);
            xQueueReset(internal->event_queue);
            tt::kernel::delayMillis(50);
            xQueueReset(internal->event_queue);
        }
    }
    return 0;
}

// endregion

// region Pin acquisition

static error_t acquire_button(const GpioPinSpec& pin, void (*callback)(void*), void* arg, GpioDescriptor** out_descriptor) {
    // Digital pull-up; the buttons pull the pin low while pressed. Listen for the release
    // (positive edge) rather than the press - if we listen to the press, these buttons can
    // generate more than one signal when held down (mirrors the original unPhone init).
    gpio_flags_t flags = GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP;
    flags = GPIO_FLAG_INTERRUPT_TO_OPTIONS(flags, GPIO_INTERRUPT_POS_EDGE);

    auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, flags, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        return ERROR_RESOURCE;
    }

    if (error == ERROR_NONE) {
        error = gpio_descriptor_add_callback(descriptor, callback, arg);
    }
    if (error == ERROR_NONE) {
        error = gpio_descriptor_enable_interrupt(descriptor);
    }

    if (error != ERROR_NONE) {
        gpio_descriptor_remove_callback(descriptor);
        gpio_descriptor_release(descriptor);
        return error;
    }

    *out_descriptor = descriptor;
    return ERROR_NONE;
}

static void release_button(GpioDescriptor*& descriptor) {
    if (descriptor == nullptr) {
        return;
    }
    gpio_descriptor_disable_interrupt(descriptor);
    gpio_descriptor_remove_callback(descriptor);
    gpio_descriptor_release(descriptor);
    descriptor = nullptr;
}

// endregion

extern "C" {

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) UnphoneNavButtonsInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->event_queue = xQueueCreate(4, sizeof(int));
    if (internal->event_queue == nullptr) {
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = acquire_button(config->pin_button1, on_button1, internal, &internal->pin_button1);
    if (error == ERROR_NONE) {
        error = acquire_button(config->pin_button2, on_button2, internal, &internal->pin_button2);
    }
    if (error == ERROR_NONE) {
        error = acquire_button(config->pin_button3, on_button3, internal, &internal->pin_button3);
    }

    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to acquire nav button pins");
        release_button(internal->pin_button1);
        release_button(internal->pin_button2);
        release_button(internal->pin_button3);
        vQueueDelete(internal->event_queue);
        delete internal;
        return error;
    }

    internal->thread.setName("unphone_nav_buttons");
    internal->thread.setPriority(tt::Thread::Priority::High);
    internal->thread.setStackSize(3072);
    internal->thread.setMainFunction([internal] { return nav_buttons_thread_main(internal); });
    internal->thread.start();

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);

    if (internal->thread.getState() != tt::Thread::State::Stopped) {
        internal->thread_interrupt_requested = true;
        internal->thread.join();
    }

    release_button(internal->pin_button1);
    release_button(internal->pin_button2);
    release_button(internal->pin_button3);
    vQueueDelete(internal->event_queue);

    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

extern Module unphone_module;

Driver unphone_nav_buttons_driver = {
    .name = "unphone_nav_buttons",
    .compatible = (const char*[]) { "unphone,nav-buttons", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &unphone_module,
    .internal = nullptr
};

}
