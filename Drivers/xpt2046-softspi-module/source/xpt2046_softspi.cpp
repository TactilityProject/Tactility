// SPDX-License-Identifier: Apache-2.0
#include <drivers/xpt2046_softspi.h>
#include <xpt2046_softspi_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/pointer.h>
#include <tactility/delay.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <cstdlib>

#define TAG "XPT2046SoftSPI"
#define GET_CONFIG(device) (static_cast<const Xpt2046SoftSpiConfig*>((device)->config))

namespace {

constexpr uint8_t CMD_READ_X = 0xD0;
constexpr uint8_t CMD_READ_Y = 0x90;

constexpr int RAW_MIN_DEFAULT = 100;
constexpr int RAW_MAX_DEFAULT = 1900;
constexpr int RAW_VALID_MIN = 100;
constexpr int RAW_VALID_MAX = 3900;
constexpr int SAMPLE_COUNT = 8;

} // namespace

struct Xpt2046SoftSpiInternal {
    GpioDescriptor* mosi;
    GpioDescriptor* miso;
    GpioDescriptor* sck;
    GpioDescriptor* cs;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool touched;
    uint16_t x;
    uint16_t y;
};

// region Driver lifecycle

static void release_descriptors(Xpt2046SoftSpiInternal* internal) {
    if (internal->mosi != nullptr) gpio_descriptor_release(internal->mosi);
    if (internal->miso != nullptr) gpio_descriptor_release(internal->miso);
    if (internal->sck != nullptr) gpio_descriptor_release(internal->sck);
    if (internal->cs != nullptr) gpio_descriptor_release(internal->cs);
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(malloc(sizeof(Xpt2046SoftSpiInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *internal = {};

    internal->mosi = gpio_descriptor_acquire(config->pin_mosi.gpio_controller, config->pin_mosi.pin, GPIO_OWNER_GPIO);
    internal->miso = gpio_descriptor_acquire(config->pin_miso.gpio_controller, config->pin_miso.pin, GPIO_OWNER_GPIO);
    internal->sck = gpio_descriptor_acquire(config->pin_sck.gpio_controller, config->pin_sck.pin, GPIO_OWNER_GPIO);
    internal->cs = gpio_descriptor_acquire(config->pin_cs.gpio_controller, config->pin_cs.pin, GPIO_OWNER_GPIO);

    if (internal->mosi == nullptr || internal->miso == nullptr || internal->sck == nullptr || internal->cs == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO descriptors");
        release_descriptors(internal);
        free(internal);
        return ERROR_RESOURCE;
    }

    bool ok =
        gpio_descriptor_set_flags(internal->mosi, GPIO_FLAG_DIRECTION_OUTPUT) == ERROR_NONE &&
        gpio_descriptor_set_flags(internal->sck, GPIO_FLAG_DIRECTION_OUTPUT) == ERROR_NONE &&
        gpio_descriptor_set_flags(internal->cs, GPIO_FLAG_DIRECTION_OUTPUT) == ERROR_NONE &&
        gpio_descriptor_set_flags(internal->miso, GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP) == ERROR_NONE;

    // Idle state: CS high (deselected), SCK/MOSI low.
    ok = ok &&
        gpio_descriptor_set_level(internal->cs, true) == ERROR_NONE &&
        gpio_descriptor_set_level(internal->sck, false) == ERROR_NONE &&
        gpio_descriptor_set_level(internal->mosi, false) == ERROR_NONE;

    if (!ok) {
        LOG_E(TAG, "Failed to configure GPIO pins");
        release_descriptors(internal);
        free(internal);
        return ERROR_RESOURCE;
    }

    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    release_descriptors(internal);
    free(internal);
    return ERROR_NONE;
}

// endregion

// region Bit-banged SPI

// XPT2046 protocol: 8-bit command out, 12-bit conversion result back (MSB first), clocked
// manually since no hardware SPI peripheral is available for this pin set (see the binding's
// description). 1us clock half-period is well within the XPT2046's timing budget (it's rated
// for a multi-MHz SPI clock; this bit-bang loop runs orders of magnitude slower).
static int read_spi_command(Xpt2046SoftSpiInternal* internal, uint8_t command) {
    int result = 0;

    gpio_descriptor_set_level(internal->cs, false);
    delay_micros(1);

    for (int i = 7; i >= 0; i--) {
        gpio_descriptor_set_level(internal->mosi, (command & (1 << i)) != 0);
        gpio_descriptor_set_level(internal->sck, true);
        delay_micros(1);
        gpio_descriptor_set_level(internal->sck, false);
        delay_micros(1);
    }

    for (int i = 11; i >= 0; i--) {
        gpio_descriptor_set_level(internal->sck, true);
        delay_micros(1);
        bool level = false;
        gpio_descriptor_get_level(internal->miso, &level);
        if (level) {
            result |= (1 << i);
        }
        gpio_descriptor_set_level(internal->sck, false);
        delay_micros(1);
    }

    gpio_descriptor_set_level(internal->cs, true);

    return result;
}

// endregion

// region PointerApi

static error_t xpt2046_softspi_enter_sleep(Device*) {
    // No software-controlled power-down for this bit-banged variant; nothing to do.
    return ERROR_NONE;
}

static error_t xpt2046_softspi_exit_sleep(Device*) {
    return ERROR_NONE;
}

// Takes and averages several raw samples (rejecting out-of-range noise) then maps them onto
// [0, x_max]/[0, y_max], applying swap/mirror. Mirrors the deprecated HAL's Xpt2046SoftSpi driver.
static error_t xpt2046_softspi_read_data(Device* device, TickType_t timeout) {
    (void)timeout;
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    int total_x = 0;
    int total_y = 0;
    int valid_samples = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        const int raw_x = read_spi_command(internal, CMD_READ_X);
        const int raw_y = read_spi_command(internal, CMD_READ_Y);

        if (raw_x > RAW_VALID_MIN && raw_x < RAW_VALID_MAX && raw_y > RAW_VALID_MIN && raw_y < RAW_VALID_MAX) {
            total_x += raw_x;
            total_y += raw_y;
            valid_samples++;
        }

        delay_millis(1);
    }

    if (valid_samples < 3) {
        internal->touched = false;
        return ERROR_NONE;
    }

    const int raw_x = total_x / valid_samples;
    const int raw_y = total_y / valid_samples;

    int mapped_x = (raw_x - RAW_MIN_DEFAULT) * static_cast<int>(config->x_max) / (RAW_MAX_DEFAULT - RAW_MIN_DEFAULT);
    int mapped_y = (raw_y - RAW_MIN_DEFAULT) * static_cast<int>(config->y_max) / (RAW_MAX_DEFAULT - RAW_MIN_DEFAULT);

    if (internal->swap_xy) {
        const int swapped = mapped_x;
        mapped_x = mapped_y;
        mapped_y = swapped;
    }
    if (internal->mirror_x) {
        mapped_x = static_cast<int>(config->x_max) - mapped_x;
    }
    if (internal->mirror_y) {
        mapped_y = static_cast<int>(config->y_max) - mapped_y;
    }

    if (mapped_x < 0) mapped_x = 0;
    if (mapped_x > static_cast<int>(config->x_max)) mapped_x = static_cast<int>(config->x_max);
    if (mapped_y < 0) mapped_y = 0;
    if (mapped_y > static_cast<int>(config->y_max)) mapped_y = static_cast<int>(config->y_max);

    internal->x = static_cast<uint16_t>(mapped_x);
    internal->y = static_cast<uint16_t>(mapped_y);
    internal->touched = true;
    return ERROR_NONE;
}

static bool xpt2046_softspi_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    (void)strength;
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));

    if (!internal->touched || max_point_count < 1) {
        *point_count = 0;
        return false;
    }

    *x = internal->x;
    *y = internal->y;
    *point_count = 1;
    return true;
}

static error_t xpt2046_softspi_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    internal->swap_xy = swap;
    return ERROR_NONE;
}

static error_t xpt2046_softspi_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    *swap = internal->swap_xy;
    return ERROR_NONE;
}

static error_t xpt2046_softspi_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    internal->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t xpt2046_softspi_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    *mirror = internal->mirror_x;
    return ERROR_NONE;
}

static error_t xpt2046_softspi_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    internal->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t xpt2046_softspi_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Xpt2046SoftSpiInternal*>(device_get_driver_data(device));
    *mirror = internal->mirror_y;
    return ERROR_NONE;
}

// endregion

static const PointerApi xpt2046_softspi_pointer_api = {
    .enter_sleep = xpt2046_softspi_enter_sleep,
    .exit_sleep = xpt2046_softspi_exit_sleep,
    .read_data = xpt2046_softspi_read_data,
    .get_touched_points = xpt2046_softspi_get_touched_points,
    .set_swap_xy = xpt2046_softspi_set_swap_xy,
    .get_swap_xy = xpt2046_softspi_get_swap_xy,
    .set_mirror_x = xpt2046_softspi_set_mirror_x,
    .get_mirror_x = xpt2046_softspi_get_mirror_x,
    .set_mirror_y = xpt2046_softspi_set_mirror_y,
    .get_mirror_y = xpt2046_softspi_get_mirror_y,
};

Driver xpt2046_softspi_driver = {
    .name = "xpt2046_softspi",
    .compatible = (const char*[]) { "xptek,xpt2046-softspi", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &xpt2046_softspi_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &xpt2046_softspi_module,
    .internal = nullptr
};
