// SPDX-License-Identifier: Apache-2.0
#include <new>

#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/lora.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <drivers/sx1262.h>
#include <drivers/sx1262_radio.h>

#include <driver/gpio.h>

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

    void release_pins() {
        release_pin(&pin_reset);
        release_pin(&pin_busy);
        release_pin(&pin_dio1);
        release_pin(&pin_enable);
        release_pin(&pin_antenna_select);
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
 * so pins behind an IO expander can't back them.
 */
static GpioDescriptor* acquire_native_pin(const struct GpioPinSpec& spec, const char* pin_name, gpio_num_t* native_pin) {
    if (spec.gpio_controller == nullptr) {
        LOG_E(TAG, "Pin \"%s\" is not set", pin_name);
        return nullptr;
    }

    auto* descriptor = gpio_descriptor_acquire(spec.gpio_controller, spec.pin, GPIO_OWNER_GPIO);
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
 * Controllers may not support ACTIVE_LOW on outputs (e.g. xl9555), so polarity from the pin spec is applied here.
 */
static error_t acquire_and_drive_pin(const struct GpioPinSpec& spec, const char* pin_name, bool active, GpioDescriptor** out_descriptor) {
    if (spec.gpio_controller == nullptr) {
        *out_descriptor = nullptr;
        return ERROR_NONE;
    }

    auto* descriptor = gpio_descriptor_acquire(spec.gpio_controller, spec.pin, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire pin \"%s\"", pin_name);
        return ERROR_RESOURCE;
    }

    const bool level = (spec.flags & GPIO_FLAG_ACTIVE_LOW) ? !active : active;
    if (gpio_descriptor_set_flags(descriptor, GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE ||
        gpio_descriptor_set_level(descriptor, level) != ERROR_NONE) {
        LOG_E(TAG, "Failed to drive pin \"%s\"", pin_name);
        gpio_descriptor_release(descriptor);
        return ERROR_RESOURCE;
    }

    LOG_I(TAG, "Pin \"%s\" (controller \"%s\" pin %u) driven %s", pin_name, spec.gpio_controller->name, spec.pin, level ? "high" : "low");
    *out_descriptor = descriptor;
    return ERROR_NONE;
}

static void set_pin_active(GpioDescriptor* descriptor, const struct GpioPinSpec& spec, bool active) {
    if (descriptor != nullptr) {
        const bool level = (spec.flags & GPIO_FLAG_ACTIVE_LOW) ? !active : active;
        gpio_descriptor_set_level(descriptor, level);
    }
}

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

    auto* data = new (std::nothrow) Sx1262Internal();
    if (data == nullptr) return ERROR_OUT_OF_MEMORY;

    gpio_num_t pin_reset = GPIO_NUM_NC;
    gpio_num_t pin_busy = GPIO_NUM_NC;
    gpio_num_t pin_dio1 = GPIO_NUM_NC;

    data->pin_reset = acquire_native_pin(config->pin_reset, "pin-reset", &pin_reset);
    data->pin_busy = acquire_native_pin(config->pin_busy, "pin-busy", &pin_busy);
    data->pin_dio1 = acquire_native_pin(config->pin_dio1, "pin-dio1", &pin_dio1);

    if (data->pin_reset == nullptr || data->pin_busy == nullptr || data->pin_dio1 == nullptr) {
        data->release_pins();
        delete data;
        return ERROR_RESOURCE;
    }

    // Select the antenna path first, then power the module: the RF switch should
    // never be indeterminate while the radio is powered.
    if (acquire_and_drive_pin(config->pin_antenna_select, "pin-antenna-select", true, &data->pin_antenna_select) != ERROR_NONE ||
        acquire_and_drive_pin(config->pin_enable, "pin-enable", true, &data->pin_enable) != ERROR_NONE) {
        data->release_pins();
        delete data;
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
        delete data;
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

    data->radio = new (std::nothrow) Sx1262Radio(settings);
    if (data->radio == nullptr) {
        data->release_pins();
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }

    // Cheap GPIO-only sanity check that a live chip answers on these pins,
    // so a miswired or unpowered module fails at device start instead of
    // surfacing later as an opaque RadioLib error on the radio thread.
    if (data->radio->probe() != ERROR_NONE) {
        delete data->radio;
        // Power down first, then release the antenna path (mirror of the start order)
        set_pin_active(data->pin_enable, config->pin_enable, false);
        set_pin_active(data->pin_antenna_select, config->pin_antenna_select, false);
        data->release_pins();
        delete data;
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
    delete data->radio;
    data->radio = nullptr;

    // Power down first, then release the antenna path (mirror of the start order)
    auto* config = GET_CONFIG(device);
    set_pin_active(data->pin_enable, config->pin_enable, false);
    set_pin_active(data->pin_antenna_select, config->pin_antenna_select, false);

    data->release_pins();
    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

// region LoraApi

static Sx1262Radio* get_radio(Device* device) {
    auto* data = GET_DATA(device);
    return (data != nullptr) ? data->radio : nullptr;
}

static error_t api_get_radio_state(Device* device, enum LoraRadioState* state) {
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

static error_t api_set_modulation(Device* device, enum LoraModulation modulation) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->setModulation(modulation);
}

static error_t api_get_modulation(Device* device, enum LoraModulation* modulation) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    *modulation = radio->getModulation();
    return ERROR_NONE;
}

static bool api_can_transmit(Device* device, enum LoraModulation modulation) {
    auto* radio = get_radio(device);
    return (radio != nullptr) && radio->canTransmit(modulation);
}

static bool api_can_receive(Device* device, enum LoraModulation modulation) {
    auto* radio = get_radio(device);
    return (radio != nullptr) && radio->canReceive(modulation);
}

static error_t api_set_parameter(Device* device, enum LoraParameter parameter, float value) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->setParameter(parameter, value);
}

static error_t api_get_parameter(Device* device, enum LoraParameter parameter, float* value) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->getParameter(parameter, value);
}

static error_t api_transmit(Device* device, const uint8_t* data, size_t length, LoraTxId* id) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->transmit(data, length, id);
}

static error_t api_add_rx_callback(Device* device, void* callback_context, LoraRxCallback callback) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->addRxCallback(callback_context, callback);
}

static error_t api_remove_rx_callback(Device* device, LoraRxCallback callback) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->removeRxCallback(callback);
}

static error_t api_add_state_callback(Device* device, void* callback_context, LoraStateCallback callback) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->addStateCallback(callback_context, callback);
}

static error_t api_remove_state_callback(Device* device, LoraStateCallback callback) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->removeStateCallback(callback);
}

static error_t api_add_tx_callback(Device* device, void* callback_context, LoraTxCallback callback) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->addTxCallback(callback_context, callback);
}

static error_t api_remove_tx_callback(Device* device, LoraTxCallback callback) {
    auto* radio = get_radio(device);
    if (radio == nullptr) return ERROR_INVALID_STATE;
    return radio->removeTxCallback(callback);
}

static const struct LoraApi sx1262_lora_api = {
    .get_radio_state = api_get_radio_state,
    .set_enabled = api_set_enabled,
    .set_modulation = api_set_modulation,
    .get_modulation = api_get_modulation,
    .can_transmit = api_can_transmit,
    .can_receive = api_can_receive,
    .set_parameter = api_set_parameter,
    .get_parameter = api_get_parameter,
    .transmit = api_transmit,
    .add_rx_callback = api_add_rx_callback,
    .remove_rx_callback = api_remove_rx_callback,
    .add_state_callback = api_add_state_callback,
    .remove_state_callback = api_remove_state_callback,
    .add_tx_callback = api_add_tx_callback,
    .remove_tx_callback = api_remove_tx_callback,
};

// endregion

extern Module sx126x_module;

Driver sx1262_driver = {
    .name = "sx1262",
    .compatible = (const char*[]) { "semtech,sx1262", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &sx1262_lora_api,
    .device_type = &LORA_TYPE,
    .owner = &sx126x_module,
    .internal = nullptr
};

} // extern "C"
