// SPDX-License-Identifier: Apache-2.0
#include <drivers/cst328.h>
#include <cst328_module.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/pointer.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <cstdlib>
#include <utility>

#define TAG "CST328"
#define GET_CONFIG(device) (static_cast<const Cst328Config*>((device)->config))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// The CST328/CST226SE protocol addresses registers via a 2-byte [class, sub-register] prefix
// (not a plain 1-byte register index), sent as a raw write, optionally followed by a read of the
// response - ported from LilyGO's SensorLib TouchClassCST226.cpp (touch/TouchClassCST226.cpp),
// the vendor driver actually used for the T-Deck Pro's CST328.
static constexpr uint8_t CST328_MAX_POINTS = 5;
static constexpr uint8_t STATUS_BUFFER_SIZE = 28;

struct Cst328Internal {
    int16_t res_x;
    int16_t res_y;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    uint8_t point_count;
    int16_t x[CST328_MAX_POINTS];
    int16_t y[CST328_MAX_POINTS];
    uint8_t strength[CST328_MAX_POINTS];
};

static bool write2(Device* i2c, uint8_t address, uint8_t b0, uint8_t b1) {
    uint8_t cmd[2] = { b0, b1 };
    return i2c_controller_write(i2c, address, cmd, sizeof(cmd), I2C_TIMEOUT) == ERROR_NONE;
}

static bool query(Device* i2c, uint8_t address, uint8_t b0, uint8_t b1, uint8_t* out, size_t out_len) {
    uint8_t cmd[2] = { b0, b1 };
    return i2c_controller_write_read(i2c, address, cmd, sizeof(cmd), out, out_len, I2C_TIMEOUT) == ERROR_NONE;
}

// region Driver lifecycle

// Some boards wire a dedicated reset GPIO (e.g. the T-Deck Pro's GPIO45); others rely on the
// chip's own software reset command instead.
static void perform_reset(Device* i2c, uint8_t address, const Cst328Config* config) {
    if (config->pin_reset.gpio_controller != nullptr) {
        auto* rst = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_OWNER_GPIO);
        if (rst != nullptr) {
            gpio_descriptor_set_flags(rst, GPIO_FLAG_DIRECTION_OUTPUT);
            gpio_descriptor_set_level(rst, config->reset_active_high);
            delay_millis(100);
            gpio_descriptor_set_level(rst, !config->reset_active_high);
            gpio_descriptor_release(rst);
            delay_millis(100);
            return;
        }
    }

    write2(i2c, address, 0xD1, 0x0E);
    delay_millis(20);
}

// Enters command mode, reads the firmware checkcode and panel resolution, verifies the firmware
// is genuinely present, then exits command mode. Ported from TouchClassCST226::initImpl().
static bool identify_and_configure(Device* i2c, uint8_t address, Cst328Internal* internal) {
    if (!write2(i2c, address, 0xD1, 0x01)) {
        return false;
    }
    delay_millis(10);

    uint8_t buffer[8];
    if (!query(i2c, address, 0xD1, 0xFC, buffer, 4)) {
        return false;
    }
    uint32_t checkcode = (static_cast<uint32_t>(buffer[3]) << 24) | (static_cast<uint32_t>(buffer[2]) << 16) |
        (static_cast<uint32_t>(buffer[1]) << 8) | buffer[0];

    if (!query(i2c, address, 0xD1, 0xF8, buffer, 4)) {
        return false;
    }
    internal->res_x = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
    internal->res_y = static_cast<int16_t>((buffer[3] << 8) | buffer[2]);

    if (!query(i2c, address, 0xD2, 0x08, buffer, 8)) {
        return false;
    }
    uint32_t fw_version = (static_cast<uint32_t>(buffer[3]) << 24) | (static_cast<uint32_t>(buffer[2]) << 16) |
        (static_cast<uint32_t>(buffer[1]) << 8) | buffer[0];

    if (fw_version == 0xA5A5A5A5) {
        LOG_E(TAG, "Chip has no firmware");
        return false;
    }
    if ((checkcode & 0xFFFF0000) != 0xCACA0000) {
        LOG_E(TAG, "Firmware info read error");
        return false;
    }

    LOG_I(TAG, "CST328 identified: %dx%d, firmware 0x%08lX", internal->res_x, internal->res_y, (unsigned long)fw_version);
    return write2(i2c, address, 0xD1, 0x09); // exit command mode
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Cst328Internal*>(calloc(1, sizeof(Cst328Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;

    perform_reset(parent, config->address, config);

    if (!identify_and_configure(parent, config->address, internal)) {
        // Non-fatal: matches the tolerant bring-up pattern used by other touch drivers in this
        // codebase - a transient bus hiccup on the first transaction after boot shouldn't take
        // the whole kernel down. The device just won't report touches until it responds.
        LOG_W(TAG, "CST328 identity not confirmed (continuing)");
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cst328Internal*>(device_get_driver_data(device));
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region PointerApi

static error_t cst328_enter_sleep(Device* device) {
    auto* parent = device_get_parent(device);
    const auto* config = GET_CONFIG(device);
    return write2(parent, config->address, 0xD1, 0x05) ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst328_exit_sleep(Device* device) {
    auto* parent = device_get_parent(device);
    const auto* config = GET_CONFIG(device);
    perform_reset(parent, config->address, config);
    return ERROR_NONE;
}

static error_t cst328_read_data(Device* device, TickType_t /*timeout*/) {
    auto* internal = static_cast<Cst328Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* parent = device_get_parent(device);

    uint8_t buffer[STATUS_BUFFER_SIZE];
    uint8_t reg = 0x00;
    if (i2c_controller_write_read(parent, config->address, &reg, 1, buffer, sizeof(buffer), I2C_TIMEOUT) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    // Home-button gesture frame (T-Deck Pro doesn't have one wired, but the vendor protocol
    // reports it): no touch point data.
    bool is_home_gesture = buffer[0] == 0x83 && buffer[1] == 0x17 && buffer[5] == 0x80;
    if (is_home_gesture || buffer[6] != 0xAB || buffer[0] == 0xAB || buffer[5] == 0x80) {
        internal->point_count = 0;
        return ERROR_NONE;
    }

    uint8_t point = buffer[5] & 0x7F;
    if (point > CST328_MAX_POINTS || point == 0) {
        write2(parent, config->address, 0x00, 0xAB); // acknowledge/clear
        internal->point_count = 0;
        return ERROR_NONE;
    }

    // A lone point whose status nibble isn't 0x06 is a spurious single-touch frame.
    uint8_t point1_status = buffer[0] & 0x0F;
    if (point == 1 && point1_status != 0x06) {
        point = 0;
    }

    uint8_t index = 0;
    for (uint8_t i = 0; i < point; i++) {
        int16_t raw_x = static_cast<int16_t>((buffer[index + 1] << 4) | ((buffer[index + 3] >> 4) & 0x0F));
        int16_t raw_y = static_cast<int16_t>((buffer[index + 2] << 4) | (buffer[index + 3] & 0x0F));
        uint8_t pressure = buffer[index + 4];

        if (internal->swap_xy) {
            std::swap(raw_x, raw_y);
        }
        if (internal->mirror_x && internal->res_x > 0) {
            raw_x = static_cast<int16_t>(internal->res_x - 1 - raw_x);
        }
        if (internal->mirror_y && internal->res_y > 0) {
            raw_y = static_cast<int16_t>(internal->res_y - 1 - raw_y);
        }

        internal->x[i] = raw_x;
        internal->y[i] = raw_y;
        internal->strength[i] = pressure;

        // First point's slot is 7 bytes wide (id/status + x-hi + y-hi + xy-lo + pressure + 2
        // reserved); subsequent points are 5 bytes wide.
        index = static_cast<uint8_t>((i == 0) ? (index + 7) : (index + 5));
    }
    internal->point_count = point;
    return ERROR_NONE;
}

static bool cst328_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Cst328Internal*>(device_get_driver_data(device));
    uint8_t count = internal->point_count < max_point_count ? internal->point_count : max_point_count;
    for (uint8_t i = 0; i < count; i++) {
        x[i] = static_cast<uint16_t>(internal->x[i]);
        y[i] = static_cast<uint16_t>(internal->y[i]);
        if (strength != nullptr) {
            strength[i] = internal->strength[i];
        }
    }
    *point_count = count;
    return count > 0;
}

static error_t cst328_set_swap_xy(Device* device, bool swap) {
    static_cast<Cst328Internal*>(device_get_driver_data(device))->swap_xy = swap;
    return ERROR_NONE;
}

static error_t cst328_get_swap_xy(Device* device, bool* swap) {
    *swap = static_cast<Cst328Internal*>(device_get_driver_data(device))->swap_xy;
    return ERROR_NONE;
}

static error_t cst328_set_mirror_x(Device* device, bool mirror) {
    static_cast<Cst328Internal*>(device_get_driver_data(device))->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t cst328_get_mirror_x(Device* device, bool* mirror) {
    *mirror = static_cast<Cst328Internal*>(device_get_driver_data(device))->mirror_x;
    return ERROR_NONE;
}

static error_t cst328_set_mirror_y(Device* device, bool mirror) {
    static_cast<Cst328Internal*>(device_get_driver_data(device))->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t cst328_get_mirror_y(Device* device, bool* mirror) {
    *mirror = static_cast<Cst328Internal*>(device_get_driver_data(device))->mirror_y;
    return ERROR_NONE;
}

// endregion

static const PointerApi cst328_pointer_api = {
    .enter_sleep = cst328_enter_sleep,
    .exit_sleep = cst328_exit_sleep,
    .read_data = cst328_read_data,
    .get_touched_points = cst328_get_touched_points,
    .set_swap_xy = cst328_set_swap_xy,
    .get_swap_xy = cst328_get_swap_xy,
    .set_mirror_x = cst328_set_mirror_x,
    .get_mirror_x = cst328_get_mirror_x,
    .set_mirror_y = cst328_set_mirror_y,
    .get_mirror_y = cst328_get_mirror_y,
};

Driver cst328_driver = {
    .name = "cst328",
    .compatible = (const char*[]) { "hynitron,cst328", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cst328_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &cst328_module,
    .internal = nullptr
};
