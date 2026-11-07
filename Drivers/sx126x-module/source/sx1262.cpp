// SPDX-License-Identifier: Apache-2.0
#include <tactility/Memory.hpp>

#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/radio.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <drivers/sx1262.h>
#include <drivers/sx1262_radio.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <cstring>

#define TAG "sx1262"

#define GET_CONFIG(device) ((const struct Sx1262Config*)device->config)
#define GET_DATA(device) ((struct Sx1262Internal*)device_get_driver_data(device))

extern "C" {

struct Sx1262Internal {
    Sx1262Radio* radio = nullptr;
    GpioDescriptor* pin_reset = nullptr;
    GpioDescriptor* pin_busy = nullptr;
    GpioDescriptor* pin_dio1 = nullptr;
    GpioDescriptor* pin_enable = nullptr;
    GpioDescriptor* pin_antenna_select = nullptr;
    GpioDescriptor* pin_rx_enable = nullptr;

    void release_pins() {
        release_pin(&pin_reset);
        release_pin(&pin_busy);
        release_pin(&pin_dio1);
        release_pin(&pin_enable);
        release_pin(&pin_antenna_select);
        release_pin(&pin_rx_enable);
    }

private:
    static void release_pin(GpioDescriptor** descriptor) {
        if (*descriptor != nullptr) {
            gpio_descriptor_release(*descriptor);
            *descriptor = nullptr;
        }
    }
};

/**
 * Acquire a pin that must live on an SoC GPIO controller and resolve its native pin number.
 * The reset/busy/DIO1 lines are driven directly through ESP-IDF by the RadioLib HAL,
 * so pins behind an IO expander can't back them. The caller passes the pin's direction,
 * because the devicetree pin specs carry no direction of their own.
 */
static GpioDescriptor* acquire_native_pin(const struct GpioPinSpec& spec, const char* pin_name, gpio_flags_t flags, gpio_num_t* native_pin) {
    if (spec.gpio_controller == nullptr) {
        LOG_E(TAG, "Pin \"%s\" is not set", pin_name);
        return nullptr;
    }

    auto* descriptor = gpio_descriptor_acquire(spec.gpio_controller, spec.pin, flags, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire pin \"%s\"", pin_name);
        return nullptr;
    }

    if (gpio_descriptor_get_native_pin_number(descriptor, native_pin) != ERROR_NONE) {
        LOG_E(TAG, "Pin \"%s\" must be an SoC GPIO", pin_name);
        gpio_descriptor_release(descriptor);
        return nullptr;
    }

    return descriptor;
}

/**
 * Acquire an optional control pin (may sit behind an IO expander) and drive it to the given logical state.
 * The pin spec's ACTIVE_LOW flag makes the descriptor invert the level.
 */
static error_t acquire_and_drive_pin(const struct GpioPinSpec& spec, const char* pin_name, bool active, GpioDescriptor** out_descriptor) {
    if (spec.gpio_controller == nullptr) {
        *out_descriptor = nullptr;
        return ERROR_NONE;
    }

    auto* descriptor = gpio_descriptor_acquire(spec.gpio_controller, spec.pin, GPIO_FLAG_DIRECTION_OUTPUT | (spec.flags & GPIO_FLAG_ACTIVE_LOW), GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire pin \"%s\"", pin_name);
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_set_level(descriptor, active) != ERROR_NONE) {
        LOG_E(TAG, "Failed to drive pin \"%s\"", pin_name);
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    LOG_I(TAG, "Pin \"%s\" (controller \"%s\" pin %u) driven %s", pin_name, spec.gpio_controller->name, spec.pin, active ? "active" : "inactive");
    *out_descriptor = descriptor;
    return ERROR_NONE;
}

static void set_pin_active(GpioDescriptor* descriptor, bool active) {
    if (descriptor != nullptr) {
        gpio_descriptor_set_level(descriptor, active);
    }
}

// region Probe

constexpr uint8_t SX126X_COMMAND_READ_REGISTER = 0x1D;
constexpr uint16_t SX126X_REGISTER_VERSION_STRING = 0x0320;
constexpr size_t SX126X_VERSION_STRING_LENGTH = 16;
// Datasheet: the chip is ready about 3.5 ms after a reset
constexpr int PROBE_READY_TIMEOUT_MILLIS = 10;

/** Acquire a pin for probing without logging, as probing repeats while the hardware is absent */
static GpioDescriptor* acquire_probe_pin(const struct GpioPinSpec& spec, gpio_flags_t flags) {
    if (spec.gpio_controller == nullptr) {
        return nullptr;
    }
    return gpio_descriptor_acquire(spec.gpio_controller, spec.pin, flags, GPIO_OWNER_GPIO);
}

static bool wait_until_ready(GpioDescriptor* busy) {
    for (int elapsed = 0; elapsed <= PROBE_READY_TIMEOUT_MILLIS; elapsed++) {
        bool level = true;
        if (gpio_descriptor_get_level(busy, &level) == ERROR_NONE && !level) {
            return true;
        }
        delay_millis(1);
    }
    return false;
}

/** Read the version string register through a temporary SPI device, as RadioLib does when it looks for the chip */
static bool read_version_string(Device* device, char* version) {
    auto* parent = device_get_parent(device);
    auto* config = GET_CONFIG(device);
    auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);

    struct GpioPinSpec cs_pin_spec;
    if (esp32_spi_get_cs_pin(device, &cs_pin_spec) != ERROR_NONE) {
        return false;
    }
    const auto pin_cs = static_cast<gpio_num_t>(cs_pin_spec.pin);
    gpio_set_direction(pin_cs, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_cs, 1);

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = (int)config->spi_frequency_khz * 1000;
    device_config.mode = 0;
    device_config.spics_io_num = -1;
    device_config.queue_size = 1;
    spi_device_handle_t handle;
    if (spi_bus_add_device(spi_config->host, &device_config, &handle) != ESP_OK) {
        return false;
    }

    uint8_t tx[4 + SX126X_VERSION_STRING_LENGTH] = {
        SX126X_COMMAND_READ_REGISTER,
        (uint8_t)(SX126X_REGISTER_VERSION_STRING >> 8),
        (uint8_t)(SX126X_REGISTER_VERSION_STRING & 0xFF),
    };
    uint8_t rx[sizeof(tx)] = {};
    spi_transaction_t transaction = {};
    transaction.length = sizeof(tx) * 8;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;

    spi_controller_lock(parent);
    spi_device_acquire_bus(handle, portMAX_DELAY);
    gpio_set_level(pin_cs, 0);
    const esp_err_t result = spi_device_polling_transmit(handle, &transaction);
    gpio_set_level(pin_cs, 1);
    spi_device_release_bus(handle);
    spi_controller_unlock(parent);
    spi_bus_remove_device(handle);

    // The response starts after the command, the 2 address bytes and the status byte
    memcpy(version, rx + 4, SX126X_VERSION_STRING_LENGTH);
    return result == ESP_OK;
}

/** Whether another started device uses the same chip-select line, e.g. another variant of an add-on in the same slot */
static bool is_slot_in_use(Device* device) {
    struct Context {
        Device* device;
        bool in_use;
    } context = { device, false };
    device_for_each_child(device_get_parent(device), &context, [](Device* sibling, void* context_pointer) {
        auto* context = static_cast<Context*>(context_pointer);
        if (sibling != context->device && sibling->address == context->device->address && device_is_ready(sibling)) {
            context->in_use = true;
            return false;
        }
        return true;
    });
    return context.in_use;
}

/** Power the module, reset it and check that it reports an SX126x version string */
static error_t detect_chip(Device* device) {
    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &SPI_CONTROLLER_TYPE || !device_is_ready(parent)) {
        return ERROR_INVALID_STATE;
    }
    if (is_slot_in_use(device)) {
        return ERROR_NOT_FOUND;
    }

    auto* config = GET_CONFIG(device);
    Sx1262Internal pins;
    pins.pin_reset = acquire_probe_pin(config->pin_reset, GPIO_FLAG_DIRECTION_OUTPUT);
    pins.pin_busy = acquire_probe_pin(config->pin_busy, GPIO_FLAG_DIRECTION_INPUT);
    pins.pin_antenna_select = acquire_probe_pin(config->pin_antenna_select, GPIO_FLAG_DIRECTION_OUTPUT);
    pins.pin_enable = acquire_probe_pin(config->pin_enable, GPIO_FLAG_DIRECTION_OUTPUT);
    const bool acquired = pins.pin_reset != nullptr && pins.pin_busy != nullptr &&
        (pins.pin_antenna_select != nullptr || config->pin_antenna_select.gpio_controller == nullptr) &&
        (pins.pin_enable != nullptr || config->pin_enable.gpio_controller == nullptr);

    bool found = false;
    if (acquired) {
        set_pin_active(pins.pin_antenna_select, true);
        set_pin_active(pins.pin_enable, true);
        if (pins.pin_enable != nullptr) {
            delay_millis(10);
        }

        // Datasheet: NRESET must be held low for at least 100 us
        gpio_descriptor_set_level(pins.pin_reset, false);
        delay_micros(200);
        gpio_descriptor_set_level(pins.pin_reset, true);

        char version[SX126X_VERSION_STRING_LENGTH] = {};
        found = wait_until_ready(pins.pin_busy) && read_version_string(device, version) && strncmp(version, "SX126", 5) == 0;

        // A found module stays powered: switching it off right before the device starts can leave it
        // without a clean power-on reset
        if (!found) {
            set_pin_active(pins.pin_enable, false);
            set_pin_active(pins.pin_antenna_select, false);
        }
    }

    pins.release_pins();
    return found ? ERROR_NONE : ERROR_NOT_FOUND;
}

static error_t probe(Device* device) {
    auto* data = GET_DATA(device);
    if (data != nullptr) {
        return data->radio->checkPresent();
    }
    return detect_chip(device);
}

// endregion

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);

    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &SPI_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not an SPI controller");
        return ERROR_INVALID_STATE;
    }

    auto* config = GET_CONFIG(device);

    // The chip-select lookup uses the node's unit address; reg exists to satisfy the
    // devicetree convention for addressed nodes, so reject a board definition where
    // the two disagree instead of silently using one of them.
    if (config->reg != device->address) {
        LOG_E(TAG, "reg (%d) does not match the node's unit address (%d)", (int)config->reg, (int)device->address);
        return ERROR_INVALID_STATE;
    }

    // DIO1's interrupt is set up later through the GPIO descriptor callback API
    // (see Sx1262Radio::registerDio1Isr), which installs the shared ISR service
    // on demand and reference-counts it against the other GPIO-interrupt users.

    auto* data = tt::newObject<Sx1262Internal>();
    if (data == nullptr) return ERROR_OUT_OF_MEMORY;

    gpio_num_t pin_reset = GPIO_NUM_NC;
    gpio_num_t pin_busy = GPIO_NUM_NC;
    gpio_num_t pin_dio1 = GPIO_NUM_NC;

    // DIO1 keeps a plain input here; registerDio1Isr() adds the interrupt flags when it
    // attaches the handler, so the line stays quiet until the radio thread is ready for it.
    data->pin_reset = acquire_native_pin(config->pin_reset, "pin-reset", GPIO_FLAG_DIRECTION_OUTPUT, &pin_reset);
    data->pin_busy = acquire_native_pin(config->pin_busy, "pin-busy", GPIO_FLAG_DIRECTION_INPUT, &pin_busy);
    data->pin_dio1 = acquire_native_pin(config->pin_dio1, "pin-dio1", GPIO_FLAG_DIRECTION_INPUT, &pin_dio1);

    if (data->pin_reset == nullptr || data->pin_busy == nullptr || data->pin_dio1 == nullptr) {
        data->release_pins();
        tt::deleteObject(data);
        return ERROR_RESOURCE;
    }

    gpio_num_t pin_rx_enable = GPIO_NUM_NC;
    if (config->pin_rx_enable.gpio_controller != nullptr) {
        data->pin_rx_enable = acquire_native_pin(config->pin_rx_enable, "pin-rx-enable", GPIO_FLAG_DIRECTION_OUTPUT, &pin_rx_enable);
        if (data->pin_rx_enable == nullptr) {
            data->release_pins();
            tt::deleteObject(data);
            return ERROR_RESOURCE;
        }
    }

    // Select the antenna path first, then power the module: the RF switch should
    // never be indeterminate while the radio is powered.
    if (acquire_and_drive_pin(config->pin_antenna_select, "pin-antenna-select", true, &data->pin_antenna_select) != ERROR_NONE ||
        acquire_and_drive_pin(config->pin_enable, "pin-enable", true, &data->pin_enable) != ERROR_NONE) {
        data->release_pins();
        tt::deleteObject(data);
        return ERROR_RESOURCE;
    }

    // Give the supply rail time to settle before poking the chip
    if (data->pin_enable != nullptr) {
        delay_millis(10);
    }

    struct GpioPinSpec cs_pin_spec;
    if (esp32_spi_get_cs_pin(device, &cs_pin_spec) != ERROR_NONE) {
        LOG_E(TAG, "Failed to get CS pin from parent SPI controller");
        data->release_pins();
        tt::deleteObject(data);
        return ERROR_RESOURCE;
    }

    auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);

    const Sx1262Radio::Settings settings = {
        .device = device,
        .spi_controller = parent,
        .spi_host = spi_config->host,
        .spi_frequency_hz = (int)config->spi_frequency_khz * 1000,
        .pin_cs = static_cast<gpio_num_t>(cs_pin_spec.pin),
        .pin_reset = pin_reset,
        .pin_busy = pin_busy,
        .dio1 = data->pin_dio1,
        .tcxo_voltage = (float)config->tcxo_millivolts / 1000.0f,
        .use_regulator_ldo = config->use_regulator_ldo,
        .dio2_rf_switch = config->dio2_as_rf_switch,
        .pin_rx_enable = pin_rx_enable,
    };

    LOG_I(
        TAG,
        "%s: CS=%d RST=%d BUSY=%d DIO1=%d, SPI %u kHz, TCXO %u mV, DIO2-RF-switch=%s, LDO=%s",
        device->name,
        (int)settings.pin_cs,
        (int)pin_reset,
        (int)pin_busy,
        (int)pin_dio1,
        (unsigned)config->spi_frequency_khz,
        (unsigned)config->tcxo_millivolts,
        config->dio2_as_rf_switch ? "yes" : "no",
        config->use_regulator_ldo ? "yes" : "no"
    );

    data->radio = tt::newObject<Sx1262Radio>(settings);
    if (data->radio == nullptr) {
        data->release_pins();
        tt::deleteObject(data);
        return ERROR_OUT_OF_MEMORY;
    }

    // Cheap GPIO-only sanity check that a live chip answers on these pins,
    // so a miswired or unpowered module fails at device start instead of
    // surfacing later as an opaque RadioLib error on the radio thread.
    if (data->radio->probe() != ERROR_NONE) {
        tt::deleteObject(data->radio);
        // Power down first, then release the antenna path (mirror of the start order)
        set_pin_active(data->pin_enable, false);
        set_pin_active(data->pin_antenna_select, false);
        data->release_pins();
        tt::deleteObject(data);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);

    auto* data = GET_DATA(device);
    if (data == nullptr) return ERROR_NONE;

    // Stops the radio thread before teardown
    tt::deleteObject(data->radio);
    data->radio = nullptr;

    // Power down first, then release the antenna path (mirror of the start order)
    auto* config = GET_CONFIG(device);
    set_pin_active(data->pin_enable, false);
    set_pin_active(data->pin_antenna_select, false);

    data->release_pins();
    device_set_driver_data(device, nullptr);
    tt::deleteObject(data);
    return ERROR_NONE;
}

// region RadioApi

static Sx1262Radio* get_radio(Device* device) {
    auto* data = GET_DATA(device);
    return (data != nullptr) ? data->radio : nullptr;
}

static error_t api_get_radio_state(Device* device, enum RadioState* state) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    *state = radio->getState();
    return ERROR_NONE;
}

static error_t api_set_enabled(Device* device, bool enabled) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->setEnabled(enabled);
}

static error_t api_set_modulation(Device* device, enum RadioModulation modulation) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->setModulation(modulation);
}

static error_t api_get_modulation(Device* device, enum RadioModulation* modulation) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    *modulation = radio->getModulation();
    return ERROR_NONE;
}

static bool api_can_transmit(Device* device, enum RadioModulation modulation) {
    auto* radio = get_radio(device);
    return (radio != nullptr) && radio->canTransmit(modulation);
}

static bool api_can_receive(Device* device, enum RadioModulation modulation) {
    auto* radio = get_radio(device);
    return (radio != nullptr) && radio->canReceive(modulation);
}

static error_t api_set_parameter(Device* device, enum RadioParameter parameter, int32_t value) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->setParameter(parameter, value);
}

static error_t api_get_parameter(Device* device, enum RadioParameter parameter, int32_t* value) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->getParameter(parameter, value);
}

static error_t api_transmit(Device* device, const uint8_t* data, size_t length, RadioTxId* id) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->transmit(data, length, id);
}

static error_t api_get_rssi(Device* device, float* rssi) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->getRssi(rssi);
}

static error_t api_is_receiving(Device* device, bool* receiving) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->isReceiving(receiving);
}

static const struct RadioApi sx1262_radio_api = {
    .get_radio_state = api_get_radio_state,
    .set_enabled = api_set_enabled,
    .set_modulation = api_set_modulation,
    .get_modulation = api_get_modulation,
    .can_transmit = api_can_transmit,
    .can_receive = api_can_receive,
    .set_parameter = api_set_parameter,
    .get_parameter = api_get_parameter,
    .transmit = api_transmit,
    .get_rssi = api_get_rssi,
    .is_receiving = api_is_receiving,
};

// endregion

Driver sx1262_driver = {
    .name = "sx1262",
    .compatible = (const char*[]) { "semtech,sx1262", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = probe,
    .api = &sx1262_radio_api,
    .device_type = &RADIO_TYPE,
    .internal = nullptr
};

} // extern "C"
