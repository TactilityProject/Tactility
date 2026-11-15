// SPDX-License-Identifier: Apache-2.0
#include <drivers/bq27427.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <cstring>
#include <new>

#define TAG "BQ27427"
#define GET_CONFIG(device) (static_cast<const Bq27427Config*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Bq27427Internal*>(device_get_driver_data(device)))
#define GET_PARENT(device) (device_get_parent(device))

// Register map from the BQ27427 Technical Reference Manual (SLUUCD5), chapters 5 to 7
// Standard commands are 16-bit little-endian words, except StateOfHealth which is 8-bit
static constexpr uint8_t REG_CONTROL = 0x00;
static constexpr uint8_t REG_TEMPERATURE = 0x02;
static constexpr uint8_t REG_VOLTAGE = 0x04;
static constexpr uint8_t REG_FLAGS = 0x06;
static constexpr uint8_t REG_NOMINAL_AVAILABLE_CAPACITY = 0x08;
static constexpr uint8_t REG_FULL_AVAILABLE_CAPACITY = 0x0A;
static constexpr uint8_t REG_REMAINING_CAPACITY = 0x0C;
static constexpr uint8_t REG_FULL_CHARGE_CAPACITY = 0x0E;
static constexpr uint8_t REG_AVERAGE_CURRENT = 0x10;
static constexpr uint8_t REG_AVERAGE_POWER = 0x18;
static constexpr uint8_t REG_STATE_OF_CHARGE = 0x1C;
static constexpr uint8_t REG_INTERNAL_TEMPERATURE = 0x1E;
static constexpr uint8_t REG_STATE_OF_HEALTH = 0x20;
static constexpr uint8_t REG_REMAINING_CAPACITY_UNFILTERED = 0x28;
static constexpr uint8_t REG_REMAINING_CAPACITY_FILTERED = 0x2A;
static constexpr uint8_t REG_FULL_CHARGE_CAPACITY_UNFILTERED = 0x2C;
static constexpr uint8_t REG_FULL_CHARGE_CAPACITY_FILTERED = 0x2E;
static constexpr uint8_t REG_STATE_OF_CHARGE_UNFILTERED = 0x30;

// Extended commands
static constexpr uint8_t REG_DATA_CLASS = 0x3E;
static constexpr uint8_t REG_DATA_BLOCK = 0x3F;
static constexpr uint8_t REG_BLOCK_DATA = 0x40; // 32 bytes up to 0x5F
static constexpr uint8_t REG_BLOCK_DATA_CHECKSUM = 0x60;
static constexpr uint8_t REG_BLOCK_DATA_CONTROL = 0x61;

static constexpr uint16_t CONTROL_DEVICE_TYPE_VALUE = 0x0427;
static constexpr uint16_t CONTROL_SET_CFGUPDATE = 0x0013;
static constexpr uint16_t CONTROL_SEALED = 0x0020;
static constexpr uint16_t CONTROL_SOFT_RESET = 0x0042;
// Factory UNSEAL key: the same 16-bit word sent twice in a row (TRM section 4.1, it's interesting)
static constexpr uint16_t CONTROL_UNSEAL_KEY = 0x8000;

// Data memory: "Gas Gauging State" subclass (lol), offsets of the parameters we expose (TRM table 7-2).
// Values are stored big-endian.
static constexpr uint8_t DATA_CLASS_STATE = 82;
static constexpr uint8_t DATA_BLOCK_STATE = 0;
static constexpr size_t BLOCK_SIZE = 32;
static constexpr size_t OFFSET_DESIGN_CAPACITY = 6;
static constexpr size_t OFFSET_DESIGN_ENERGY = 8;
static constexpr size_t OFFSET_TERMINATE_VOLTAGE = 10;

static constexpr uint16_t DESIGN_CAPACITY_MAX_MAH = 8000;
static constexpr uint16_t DESIGN_ENERGY_MAX_MWH = 32767;
static constexpr uint16_t TERMINATE_VOLTAGE_MIN_MV = 2500;
static constexpr uint16_t TERMINATE_VOLTAGE_MAX_MV = 3700;

// Timing from the TRM: 
// - 66 us bus-free time between packets (section 3.3), at least 1100 ms
// - after SET_CFGUPDATE before touching data memory (section 5.1.9), 
// - CONFIG UPDATE entry and exit take up to 1 s (section 4.1).
static constexpr uint32_t BUS_FREE_US = 66;
static constexpr uint32_t CFGUPDATE_SETTLE_MS = 1100;
static constexpr uint32_t CFGUPDATE_POLL_INTERVAL_MS = 500;
static constexpr int CFGUPDATE_POLL_ATTEMPTS = 6;
// The TRM gives no settling time between selecting a data block and reading it sigh
static constexpr uint32_t BLOCK_SELECT_SETTLE_MS = 10;

static constexpr uint32_t STATUS_CACHE_MS = 1000;

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

struct Bq27427Internal {
    Mutex mutex;
    Device* power_supply_device = nullptr;
    Bq27427Status cached_status = {};
    TickType_t cached_at_ticks = 0;
    bool has_cached_status = false;

    Bq27427Internal() { mutex_construct(&mutex); }
    ~Bq27427Internal() { mutex_destruct(&mutex); }
};

class ScopedLock {
    Mutex* mutex;
public:
    explicit ScopedLock(Bq27427Internal* internal) : mutex(&internal->mutex) { mutex_lock(mutex); }
    ~ScopedLock() { mutex_unlock(mutex); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

extern "C" {

// region Helpers

static error_t read_u16(Device* device, uint8_t reg, uint16_t* value) {
    error_t error = i2c_controller_register16le_get(device_get_parent(device), GET_CONFIG(device)->address, reg, value, I2C_TIMEOUT);
    delay_micros(BUS_FREE_US);
    return error;
}

static error_t read_u8(Device* device, uint8_t reg, uint8_t* value) {
    error_t error = i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, reg, value, I2C_TIMEOUT);
    delay_micros(BUS_FREE_US);
    return error;
}

static error_t write_u8(Device* device, uint8_t reg, uint8_t value) {
    error_t error = i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, reg, value, I2C_TIMEOUT);
    delay_micros(BUS_FREE_US);
    return error;
}

static error_t read_block(Device* device, uint8_t reg, uint8_t* data, size_t size) {
    error_t error = i2c_controller_read_register(device_get_parent(device), GET_CONFIG(device)->address, reg, data, size, I2C_TIMEOUT);
    delay_micros(BUS_FREE_US);
    return error;
}

static error_t write_control(Device* device, uint16_t subcommand) {
    error_t error = write_u8(device, REG_CONTROL, static_cast<uint8_t>(subcommand & 0xFF));
    if (error != ERROR_NONE) return error;
    return write_u8(device, REG_CONTROL + 1, static_cast<uint8_t>(subcommand >> 8));
}

static error_t read_control(Device* device, uint16_t subcommand, uint16_t* value) {
    error_t error = write_control(device, subcommand);
    if (error != ERROR_NONE) return error;
    return read_u16(device, REG_CONTROL, value);
}

static uint8_t block_checksum(const uint8_t* data) {
    uint8_t sum = 0;
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return static_cast<uint8_t>(0xFF - sum);
}

static uint16_t read_be16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] << 8 | data[1]);
}

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value & 0xFF);
}

static error_t read_flags(Device* device, uint16_t* flags) {
    return read_u16(device, REG_FLAGS, flags);
}

// Polls Flags() CFGUPMODE until it matches the wanted state
static error_t wait_for_config_update_mode(Device* device, bool wanted) {
    for (int attempt = 0; attempt < CFGUPDATE_POLL_ATTEMPTS; attempt++) {
        uint16_t flags = 0;
        error_t error = read_flags(device, &flags);
        if (error != ERROR_NONE) return error;
        if (((flags & BQ27427_FLAG_CFGUPMODE) != 0) == wanted) return ERROR_NONE;
        delay_millis(CFGUPDATE_POLL_INTERVAL_MS);
    }
    return ERROR_TIMEOUT;
}

static error_t select_state_block(Device* device) {
    error_t error = write_u8(device, REG_BLOCK_DATA_CONTROL, 0x00);
    if (error != ERROR_NONE) return error;
    error = write_u8(device, REG_DATA_CLASS, DATA_CLASS_STATE);
    if (error != ERROR_NONE) return error;
    error = write_u8(device, REG_DATA_BLOCK, DATA_BLOCK_STATE);
    if (error != ERROR_NONE) return error;
    delay_millis(BLOCK_SELECT_SETTLE_MS);
    return ERROR_NONE;
}

static error_t read_state_block(Device* device, uint8_t* block) {
    error_t error = select_state_block(device);
    if (error != ERROR_NONE) return error;
    return read_block(device, REG_BLOCK_DATA, block, BLOCK_SIZE);
}

static error_t validate_design(const Bq27427Design& design) {
    if (design.capacity_mah > DESIGN_CAPACITY_MAX_MAH) return ERROR_INVALID_ARGUMENT;
    if (design.energy_mwh > DESIGN_ENERGY_MAX_MWH) return ERROR_INVALID_ARGUMENT;
    if (design.terminate_voltage_mv != 0 &&
        (design.terminate_voltage_mv < TERMINATE_VOLTAGE_MIN_MV || design.terminate_voltage_mv > TERMINATE_VOLTAGE_MAX_MV)) {
        return ERROR_INVALID_ARGUMENT;
    }
    return ERROR_NONE;
}

// Applies the non-zero design fields to a copy of the state block. Returns true when anything changed.
static bool apply_design(const Bq27427Design& design, uint8_t* block) {
    bool changed = false;
    const struct { size_t offset; uint16_t value; } fields[] = {
        { OFFSET_DESIGN_CAPACITY, design.capacity_mah },
        { OFFSET_DESIGN_ENERGY, design.energy_mwh },
        { OFFSET_TERMINATE_VOLTAGE, design.terminate_voltage_mv }
    };
    for (const auto& field : fields) {
        if (field.value != 0 && read_be16(block + field.offset) != field.value) {
            write_be16(block + field.offset, field.value);
            changed = true;
        }
    }
    return changed;
}

// Writes the modified block and verifies it by reading it back. The caller holds the lock
// and the gauge is in CONFIG UPDATE mode.
static error_t write_state_block(Device* device, const uint8_t* old_block, const uint8_t* new_block) {
    error_t error = select_state_block(device);
    if (error != ERROR_NONE) return error;

    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        if (old_block[i] == new_block[i]) continue;
        error = write_u8(device, static_cast<uint8_t>(REG_BLOCK_DATA + i), new_block[i]);
        if (error != ERROR_NONE) return error;
    }

    // The block is transferred to data memory when the complemented sum of all 32 bytes is written
    error = write_u8(device, REG_BLOCK_DATA_CHECKSUM, block_checksum(new_block));
    if (error != ERROR_NONE) return error;

    uint8_t verify[BLOCK_SIZE];
    error = read_state_block(device, verify);
    if (error != ERROR_NONE) return error;
    if (memcmp(verify, new_block, BLOCK_SIZE) != 0) {
        LOG_E(TAG, "Data memory verification failed");
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// endregion

// region Public API

static error_t read_status_locked(Device* device, Bq27427Status* status) {
    error_t error;
    const struct { uint8_t reg; uint16_t* target; } words[] = {
        { REG_TEMPERATURE, &status->temperature_decikelvin },
        { REG_VOLTAGE, &status->voltage_mv },
        { REG_FLAGS, &status->flags },
        { REG_NOMINAL_AVAILABLE_CAPACITY, &status->nominal_available_capacity_mah },
        { REG_FULL_AVAILABLE_CAPACITY, &status->full_available_capacity_mah },
        { REG_REMAINING_CAPACITY, &status->remaining_capacity_mah },
        { REG_FULL_CHARGE_CAPACITY, &status->full_charge_capacity_mah },
        { REG_STATE_OF_CHARGE, &status->state_of_charge_percent },
        { REG_INTERNAL_TEMPERATURE, &status->internal_temperature_decikelvin },
        { REG_REMAINING_CAPACITY_UNFILTERED, &status->remaining_capacity_unfiltered_mah },
        { REG_REMAINING_CAPACITY_FILTERED, &status->remaining_capacity_filtered_mah },
        { REG_FULL_CHARGE_CAPACITY_UNFILTERED, &status->full_charge_capacity_unfiltered_mah },
        { REG_FULL_CHARGE_CAPACITY_FILTERED, &status->full_charge_capacity_filtered_mah },
        { REG_STATE_OF_CHARGE_UNFILTERED, &status->state_of_charge_unfiltered_percent }
    };
    for (const auto& word : words) {
        error = read_u16(device, word.reg, word.target);
        if (error != ERROR_NONE) return error;
    }

    // Signed quantities share the unsigned read helper
    uint16_t raw = 0;
    error = read_u16(device, REG_AVERAGE_CURRENT, &raw);
    if (error != ERROR_NONE) return error;
    status->average_current_ma = static_cast<int16_t>(raw);

    error = read_u16(device, REG_AVERAGE_POWER, &raw);
    if (error != ERROR_NONE) return error;
    status->average_power_mw = static_cast<int16_t>(raw);

    return read_u8(device, REG_STATE_OF_HEALTH, &status->state_of_health_percent);
}

error_t bq27427_get_status(Device* device, Bq27427Status* status) {
    if (status == nullptr) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    const TickType_t now = get_ticks();
    if (internal->has_cached_status && (now - internal->cached_at_ticks) < pdMS_TO_TICKS(STATUS_CACHE_MS)) {
        *status = internal->cached_status;
        return ERROR_NONE;
    }

    Bq27427Status fresh = {};
    error_t error = read_status_locked(device, &fresh);
    if (error != ERROR_NONE) return error;

    internal->cached_status = fresh;
    internal->cached_at_ticks = now;
    internal->has_cached_status = true;
    *status = fresh;
    return ERROR_NONE;
}

error_t bq27427_read_control(Device* device, Bq27427ControlRead subcommand, uint16_t* value) {
    switch (subcommand) {
        case BQ27427_CONTROL_READ_STATUS:
        case BQ27427_CONTROL_READ_DEVICE_TYPE:
        case BQ27427_CONTROL_READ_FW_VERSION:
        case BQ27427_CONTROL_READ_DM_CODE:
        case BQ27427_CONTROL_READ_PREV_MACWRITE:
        case BQ27427_CONTROL_READ_CHEM_ID:
            break;
        default:
            return ERROR_INVALID_ARGUMENT;
    }
    if (value == nullptr) return ERROR_INVALID_ARGUMENT;

    ScopedLock lock(GET_INTERNAL(device));
    return read_control(device, static_cast<uint16_t>(subcommand), value);
}

error_t bq27427_send_control(Device* device, Bq27427ControlCommand command) {
    switch (command) {
        case BQ27427_CONTROL_COMMAND_BAT_INSERT:
        case BQ27427_CONTROL_COMMAND_BAT_REMOVE:
        case BQ27427_CONTROL_COMMAND_SMOOTH_SYNC:
        case BQ27427_CONTROL_COMMAND_PULSE_SOC_INT:
            break;
        default:
            return ERROR_INVALID_ARGUMENT;
    }

    ScopedLock lock(GET_INTERNAL(device));
    return write_control(device, static_cast<uint16_t>(command));
}

// Leaves CONFIG UPDATE mode and puts the gauge back in the access mode it was found in 
static error_t finish_configuration(Device* device, bool was_sealed) {
    error_t error = write_control(device, CONTROL_SOFT_RESET);
    if (error != ERROR_NONE) return error;
    error = wait_for_config_update_mode(device, false);
    if (error != ERROR_NONE) return error;

    if (!was_sealed) return ERROR_NONE;
    return write_control(device, CONTROL_SEALED);
}

error_t bq27427_configure_design(Device* device, const Bq27427Design* design) {
    if (design == nullptr) return ERROR_INVALID_ARGUMENT;
    error_t error = validate_design(*design);
    if (error != ERROR_NONE) return error;
    if (design->capacity_mah == 0 && design->energy_mwh == 0 && design->terminate_voltage_mv == 0) return ERROR_NONE;

    ScopedLock lock(GET_INTERNAL(device));

    uint16_t control_status = 0;
    error = read_control(device, BQ27427_CONTROL_READ_STATUS, &control_status);
    if (error != ERROR_NONE) return error;
    const bool was_sealed = (control_status & BQ27427_CONTROL_STATUS_SS) != 0;

    // Data memory is only accessible while unsealed
    if (was_sealed) {
        error = write_control(device, CONTROL_UNSEAL_KEY);
        if (error != ERROR_NONE) return error;
        error = write_control(device, CONTROL_UNSEAL_KEY);
        if (error != ERROR_NONE) return error;
    }

    uint8_t old_block[BLOCK_SIZE];
    error = read_state_block(device, old_block);
    if (error != ERROR_NONE) {
        // Best effort: the read error is the one worth reporting
        if (was_sealed) (void)write_control(device, CONTROL_SEALED);
        return error;
    }

    uint8_t new_block[BLOCK_SIZE];
    memcpy(new_block, old_block, BLOCK_SIZE);
    if (!apply_design(*design, new_block)) {
        return was_sealed ? write_control(device, CONTROL_SEALED) : ERROR_NONE;
    }

    LOG_I(TAG, "Programming cell parameters into data memory");
    error = write_control(device, CONTROL_SET_CFGUPDATE);
    if (error == ERROR_NONE) {
        delay_millis(CFGUPDATE_SETTLE_MS);
        error = wait_for_config_update_mode(device, true);
    }
    if (error == ERROR_NONE) {
        error = write_state_block(device, old_block, new_block);
    }

    // Leaving CONFIG UPDATE mode and resealing is attempted even after a failure, 
    // so the gauge keeps gauging heh
    error_t finish_error = finish_configuration(device, was_sealed);
    return error != ERROR_NONE ? error : finish_error;
}

// endregion

// region PowerSupplyApi

static bool supports_property(Device*, PowerSupplyProperty property) {
    switch (property) {
        case POWER_SUPPLY_PROP_CURRENT:
        case POWER_SUPPLY_PROP_VOLTAGE:
        case POWER_SUPPLY_PROP_CAPACITY:
        case POWER_SUPPLY_PROP_IS_CHARGING:
            return true;
        default:
            return false;
    }
}

static error_t get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (!supports_property(device, property)) return ERROR_NOT_SUPPORTED;

    Bq27427Status status;
    if (bq27427_get_status(GET_PARENT(device), &status) != ERROR_NONE) {
        return ERROR_NOT_FOUND;
    }

    switch (property) {
        case POWER_SUPPLY_PROP_VOLTAGE:
            out_value->int_value = status.voltage_mv;
            return ERROR_NONE;
        case POWER_SUPPLY_PROP_CURRENT:
            out_value->int_value = status.average_current_ma;
            return ERROR_NONE;
        case POWER_SUPPLY_PROP_CAPACITY:
            out_value->int_value = status.state_of_charge_percent;
            return ERROR_NONE;
        case POWER_SUPPLY_PROP_IS_CHARGING:
            // Same convention as the bq27220 driver: charging means not discharging
            out_value->int_value = (status.flags & BQ27427_FLAG_DSG) == 0 ? 1 : 0;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

static bool supports_charge_control(Device*) { return false; }
static bool is_allowed_to_charge(Device*) { return false; }
static error_t set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_quick_charge(Device*) { return false; }
static bool is_quick_charge_enabled(Device*) { return false; }
static error_t set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool supports_power_off(Device*) { return false; }
static error_t power_off(Device*) { return ERROR_NOT_SUPPORTED; }

static constexpr PowerSupplyApi BQ27427_POWER_SUPPLY_API = {
    .supports_property = supports_property,
    .get_property = get_property,
    .supports_charge_control = supports_charge_control,
    .is_allowed_to_charge = is_allowed_to_charge,
    .set_allowed_to_charge = set_allowed_to_charge,
    .supports_quick_charge = supports_quick_charge,
    .is_quick_charge_enabled = is_quick_charge_enabled,
    .set_quick_charge_enabled = set_quick_charge_enabled,
    .supports_power_off = supports_power_off,
    .power_off = power_off,
};

// Registered so driver_bind() has a valid ->internal, but never matched 
// against a devicetree node
Driver bq27427_power_supply_driver = {
    .name = "bq27427-power-supply",
    .compatible = (const char*[]) { "bq27427-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &BQ27427_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .internal = nullptr
};

// endregion

// region Driver lifecycle

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new (std::nothrow) Device { .address = 0, .name = "bq27427-power-supply", .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &bq27427_power_supply_driver);

    error = device_add(child);
    if (error != ERROR_NONE) {
        device_destruct(child);
        delete child;
        return error;
    }

    error = device_start(child);
    if (error != ERROR_NONE) {
        device_remove(child);
        device_destruct(child);
        delete child;
        return error;
    }

    out_child = child;
    return ERROR_NONE;
}

static void destroy_power_supply_child(Device* child) {
    check(device_stop(child) == ERROR_NONE);
    check(device_remove(child) == ERROR_NONE);
    check(device_destruct(child) == ERROR_NONE);
    delete child;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    auto* internal = new (std::nothrow) Bq27427Internal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    device_set_driver_data(device, internal);

    uint16_t device_type = 0;
    error_t error = bq27427_read_control(device, BQ27427_CONTROL_READ_DEVICE_TYPE, &device_type);
    if (error != ERROR_NONE || device_type != CONTROL_DEVICE_TYPE_VALUE) {
        LOG_E(TAG, "Gauge not found (device type 0x%04X)", device_type);
        device_set_driver_data(device, nullptr);
        delete internal;
        return ERROR_RESOURCE;
    }

    const auto* config = GET_CONFIG(device);
    if (config->design_capacity_mah > UINT16_MAX || config->design_energy_mwh > UINT16_MAX || config->terminate_voltage_mv > UINT16_MAX) {
        LOG_E(TAG, "Cell parameter out of range");
        device_set_driver_data(device, nullptr);
        delete internal;
        return ERROR_INVALID_ARGUMENT;
    }
    const Bq27427Design design = {
        .capacity_mah = static_cast<uint16_t>(config->design_capacity_mah),
        .energy_mwh = static_cast<uint16_t>(config->design_energy_mwh),
        .terminate_voltage_mv = static_cast<uint16_t>(config->terminate_voltage_mv)
    };

    // A failed configuration leaves the gauge working with its own parameters, so it is not fatal (cool)
    error = bq27427_configure_design(device, &design);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to program cell parameters (error %d)", static_cast<int>(error));
    }

    error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        delete internal;
        return error;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

Driver bq27427_driver = {
    .name = "bq27427",
    .compatible = (const char*[]) { "ti,bq27427", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = nullptr
};

// endregion

}
