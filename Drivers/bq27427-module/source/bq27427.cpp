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

// Sealed-to-Unsealed key pairs, each sent as two consecutive Control() words. The first pair is the factory
// default of the TRM (section 4.1).
static constexpr uint16_t UNSEAL_KEY_PAIRS[][2] = {
    { 0x8000, 0x8000 },
    { 0x0414, 0x3672 },
    { 0xFFFF, 0xFFFF }
};

// Data memory locations used outside of profiles (TRM chapter 7). 
// Parameters are stored big-endian btw
// A parameter's offset divided by BQ27427_DATA_BLOCK_SIZE is its block, 
// the remainder its index in the block.
static constexpr uint8_t DATA_CLASS_IT_CFG = 80;
static constexpr uint8_t DATA_BLOCK_DESIGN_ENERGY_SCALE = 2;
static constexpr size_t INDEX_DESIGN_ENERGY_SCALE = 17; // offset 81 of the IT Cfg subclass
static constexpr uint8_t ENERGY_SCALE_MAX = 10;

// Offset 5 of the CC Cal subclass is not documented in the TRM so i had to reference TRMNL
static constexpr uint8_t DATA_CLASS_CC_CAL = 105;
static constexpr uint8_t DATA_BLOCK_CC_CAL = 0;
static constexpr size_t INDEX_CURRENT_POLARITY = 5;
static constexpr uint8_t CC_CAL_POLARITY_BIT = 0x80;

static constexpr size_t BLOCK_SIZE = BQ27427_DATA_BLOCK_SIZE;

// Sanity limits for a plausible reading 
// strangely enough a failed transfer is likely to read back as 0xFFFF
static constexpr uint16_t VOLTAGE_MAX_MV = 10000;
static constexpr uint16_t PERCENT_MAX = 100;
static constexpr uint16_t TEMPERATURE_MIN_DECIKELVIN = 2332; // -40 C
static constexpr uint16_t TEMPERATURE_MAX_DECIKELVIN = 3732; // 100 C

// Timing from the TRM:
// - 66 us bus-free time between packets (section 3.3) 
// - at least 1100 ms after SET_CFGUPDATE before touching data memory (section 5.1.9)
// - entering and leaving CONFIG UPDATE mode takes up to 1 s (section 4.1)
static constexpr uint32_t BUS_FREE_US = 66;
static constexpr uint32_t CFGUPDATE_SETTLE_MS = 1100;
static constexpr uint32_t CFGUPDATE_POLL_INTERVAL_MS = 500;
static constexpr int CFGUPDATE_POLL_ATTEMPTS = 6;
static constexpr uint32_t UNSEAL_SETTLE_MS = 1000;
static constexpr uint32_t UNSEAL_CHECK_DELAY_MS = 50;
// The block must be loaded into the gauge's RAM before it is read back
// unfortunately the TRM doesn't give a time so I assumed 200 us is enough
static constexpr uint32_t BLOCK_CLASS_SETTLE_US = 200;
static constexpr uint32_t BLOCK_SELECT_SETTLE_MS = 5;
static constexpr uint32_t BLOCK_COMMIT_SETTLE_MS = 10;
static constexpr uint32_t READY_POLL_INTERVAL_MS = 100;

static constexpr uint32_t STATUS_CACHE_MS = 1000;

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

struct Bq27427Internal {
    Mutex mutex;
    Device* power_supply_device = nullptr;
    Bq27427Status cached_status = {};
    TickType_t cached_at_ticks = 0;
    bool has_cached_status = false;
    uint8_t energy_scale = 0;

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

static error_t read_bytes(Device* device, uint8_t reg, uint8_t* data, size_t size) {
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

// Polls Flags() CFGUPMODE until it matches the wanted state
static error_t wait_for_config_update_mode(Device* device, bool wanted) {
    for (int attempt = 0; attempt < CFGUPDATE_POLL_ATTEMPTS; attempt++) {
        uint16_t flags = 0;
        error_t error = read_u16(device, REG_FLAGS, &flags);
        if (error != ERROR_NONE) return error;
        if (((flags & BQ27427_FLAG_CFGUPMODE) != 0) == wanted) return ERROR_NONE;
        delay_millis(CFGUPDATE_POLL_INTERVAL_MS);
    }
    return ERROR_TIMEOUT;
}

static error_t select_data_block(Device* device, uint8_t data_class, uint8_t block) {
    error_t error = write_u8(device, REG_BLOCK_DATA_CONTROL, 0x00);
    if (error != ERROR_NONE) return error;
    error = write_u8(device, REG_DATA_CLASS, data_class);
    if (error != ERROR_NONE) return error;
    delay_micros(BLOCK_CLASS_SETTLE_US);
    error = write_u8(device, REG_DATA_BLOCK, block);
    if (error != ERROR_NONE) return error;
    delay_millis(BLOCK_SELECT_SETTLE_MS);
    return ERROR_NONE;
}

static error_t read_data_block(Device* device, uint8_t data_class, uint8_t block, uint8_t* data) {
    error_t error = select_data_block(device, data_class, block);
    if (error != ERROR_NONE) return error;
    return read_bytes(device, REG_BLOCK_DATA, data, BLOCK_SIZE);
}

// Writes a block and checks that the gauge accepted it 
// Note: Must be called in CONFIG UPDATE mode.
static error_t write_data_block(Device* device, uint8_t data_class, uint8_t block, const uint8_t* data) {
    error_t error = select_data_block(device, data_class, block);
    if (error != ERROR_NONE) return error;

    // The block has to be read once so that it is present in the gauge's RAM
    uint8_t current[BLOCK_SIZE];
    error = read_bytes(device, REG_BLOCK_DATA, current, BLOCK_SIZE);
    if (error != ERROR_NONE) return error;

    // Single byte writes: the TRM requires them when the bus runs faster than 100 kHz
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        error = write_u8(device, static_cast<uint8_t>(REG_BLOCK_DATA + i), data[i]);
        if (error != ERROR_NONE) return error;
    }

    // The block is transferred to data memory when the complemented sum of all 32 bytes is written
    const uint8_t checksum = block_checksum(data);
    error = write_u8(device, REG_BLOCK_DATA_CHECKSUM, checksum);
    if (error != ERROR_NONE) return error;
    delay_millis(BLOCK_COMMIT_SETTLE_MS);

    // The checksum register reads back the sum of the block that the gauge holds now
    error = select_data_block(device, data_class, block);
    if (error != ERROR_NONE) return error;
    uint8_t stored_checksum = 0;
    error = read_u8(device, REG_BLOCK_DATA_CHECKSUM, &stored_checksum);
    if (error != ERROR_NONE) return error;
    if (stored_checksum != checksum) {
        LOG_E(TAG, "Block %u/%u rejected (wrote checksum 0x%02X, read 0x%02X)", data_class, block, checksum, stored_checksum);
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// Data memory is only accessible while the gauge is unsealed 
// This puts the gauge back in the access mode it was found in (easiest approach lol)
class UnsealGuard {
    Device* device;
    bool was_sealed = false;
    bool open = false;
public:
    explicit UnsealGuard(Device* device) : device(device) {}

    error_t unseal() {
        uint16_t status = 0;
        error_t error = read_control(device, BQ27427_CONTROL_READ_STATUS, &status);
        if (error != ERROR_NONE) return error;

        was_sealed = (status & BQ27427_CONTROL_STATUS_SS) != 0;
        open = true;
        if (!was_sealed) return ERROR_NONE;

        for (const auto& pair : UNSEAL_KEY_PAIRS) {
            for (uint16_t key : pair) {
                error = write_control(device, key);
                if (error != ERROR_NONE) return error;
            }
            delay_millis(UNSEAL_CHECK_DELAY_MS);
            error = read_control(device, BQ27427_CONTROL_READ_STATUS, &status);
            if (error != ERROR_NONE) return error;
            if ((status & BQ27427_CONTROL_STATUS_SS) == 0) return ERROR_NONE;
        }
        LOG_E(TAG, "Gauge is sealed and none of the known keys unseal it");
        return ERROR_NOT_ALLOWED;
    }

    bool was_found_sealed() const { return was_sealed; }

    ~UnsealGuard() {
        if (!open || !was_sealed) return;
        if (write_control(device, CONTROL_SEALED) != ERROR_NONE) {
            LOG_E(TAG, "Failed to seal the gauge again");
        }
    }
    UnsealGuard(const UnsealGuard&) = delete;
    UnsealGuard& operator=(const UnsealGuard&) = delete;
};

static error_t read_energy_scale(Device* device, uint8_t* scale) {
    uint8_t block[BLOCK_SIZE];
    error_t error = read_data_block(device, DATA_CLASS_IT_CFG, DATA_BLOCK_DESIGN_ENERGY_SCALE, block);
    if (error != ERROR_NONE) return error;
    // 0 is not a valid scale; the TRMNL firmware treats it as 1
    *scale = block[INDEX_DESIGN_ENERGY_SCALE] > 0 ? block[INDEX_DESIGN_ENERGY_SCALE] : 1;
    return ERROR_NONE;
}

static error_t read_current_polarity_inverted(Device* device, bool* inverted) {
    uint8_t block[BLOCK_SIZE];
    error_t error = read_data_block(device, DATA_CLASS_CC_CAL, DATA_BLOCK_CC_CAL, block);
    if (error != ERROR_NONE) return error;
    *inverted = (block[INDEX_CURRENT_POLARITY] & CC_CAL_POLARITY_BIT) != 0;
    return ERROR_NONE;
}

// Reads the energy scale from the gauge once. The caller holds the lock.
static error_t ensure_energy_scale(Device* device, Bq27427Internal* internal) {
    if (internal->energy_scale != 0) return ERROR_NONE;

    UnsealGuard guard(device);
    error_t error = guard.unseal();
    if (error != ERROR_NONE) return error;

    uint8_t scale = 0;
    error = read_energy_scale(device, &scale);
    if (error != ERROR_NONE) return error;
    internal->energy_scale = scale;
    return ERROR_NONE;
}

static bool is_plausible(const Bq27427Status& status) {
    return status.state_of_charge_percent <= PERCENT_MAX &&
        status.state_of_health_percent <= PERCENT_MAX &&
        status.voltage_mv <= VOLTAGE_MAX_MV &&
        status.temperature_decikelvin >= TEMPERATURE_MIN_DECIKELVIN &&
        status.temperature_decikelvin <= TEMPERATURE_MAX_DECIKELVIN;
}

static bool is_profile_valid(const Bq27427Profile* profile) {
    if (profile == nullptr || profile->blocks == nullptr || profile->block_count == 0) return false;
    if (profile->energy_scale == 0 || profile->energy_scale > ENERGY_SCALE_MAX) return false;

    bool has_scale_block = false;
    for (size_t i = 0; i < profile->block_count; i++) {
        const auto* block = profile->blocks[i];
        if (block == nullptr) return false;
        if (block->data_class == DATA_CLASS_IT_CFG && block->block == DATA_BLOCK_DESIGN_ENERGY_SCALE) {
            if (block->data[INDEX_DESIGN_ENERGY_SCALE] != profile->energy_scale) return false;
            has_scale_block = true;
        }
    }
    // Without it the profile could never be recognized as applied
    return has_scale_block;
}

// endregion

// region Public API

static error_t read_status_locked(Device* device, uint8_t energy_scale, Bq27427Status* status) {
    error_t error;
    const struct { uint8_t reg; uint32_t* target; } capacities[] = {
        { REG_NOMINAL_AVAILABLE_CAPACITY, &status->nominal_available_capacity_mah },
        { REG_FULL_AVAILABLE_CAPACITY, &status->full_available_capacity_mah },
        { REG_REMAINING_CAPACITY, &status->remaining_capacity_mah },
        { REG_FULL_CHARGE_CAPACITY, &status->full_charge_capacity_mah },
        { REG_REMAINING_CAPACITY_UNFILTERED, &status->remaining_capacity_unfiltered_mah },
        { REG_REMAINING_CAPACITY_FILTERED, &status->remaining_capacity_filtered_mah },
        { REG_FULL_CHARGE_CAPACITY_UNFILTERED, &status->full_charge_capacity_unfiltered_mah },
        { REG_FULL_CHARGE_CAPACITY_FILTERED, &status->full_charge_capacity_filtered_mah }
    };
    const struct { uint8_t reg; uint16_t* target; } words[] = {
        { REG_TEMPERATURE, &status->temperature_decikelvin },
        { REG_VOLTAGE, &status->voltage_mv },
        { REG_FLAGS, &status->flags },
        { REG_STATE_OF_CHARGE, &status->state_of_charge_percent },
        { REG_INTERNAL_TEMPERATURE, &status->internal_temperature_decikelvin },
        { REG_STATE_OF_CHARGE_UNFILTERED, &status->state_of_charge_unfiltered_percent }
    };

    status->energy_scale = energy_scale;
    for (const auto& word : words) {
        error = read_u16(device, word.reg, word.target);
        if (error != ERROR_NONE) return error;
    }
    for (const auto& capacity : capacities) {
        uint16_t raw = 0;
        error = read_u16(device, capacity.reg, &raw);
        if (error != ERROR_NONE) return error;
        *capacity.target = static_cast<uint32_t>(raw) * energy_scale;
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

    error_t error = ensure_energy_scale(device, internal);
    if (error != ERROR_NONE) return error;

    Bq27427Status fresh = {};
    error = read_status_locked(device, internal->energy_scale, &fresh);
    if (error != ERROR_NONE) return error;
    if (!is_plausible(fresh)) return ERROR_INVALID_STATE;

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

error_t bq27427_profile_needed(Device* device, const Bq27427Profile* profile, bool* needed) {
    if (!is_profile_valid(profile) || needed == nullptr) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    uint16_t flags = 0;
    error_t error = read_u16(device, REG_FLAGS, &flags);
    if (error != ERROR_NONE) return error;
    if (flags & BQ27427_FLAG_ITPOR) {
        // RAM went back to the ROM defaults, so the scale in data memory is stale as well
        // gotta update both
        internal->energy_scale = 0;
        *needed = true;
        return ERROR_NONE;
    }

    UnsealGuard guard(device);
    error = guard.unseal();
    if (error != ERROR_NONE) return error;

    uint8_t scale = 0;
    error = read_energy_scale(device, &scale);
    if (error != ERROR_NONE) return error;
    internal->energy_scale = scale;

    bool polarity_inverted = false;
    error = read_current_polarity_inverted(device, &polarity_inverted);
    if (error != ERROR_NONE) return error;

    *needed = scale != profile->energy_scale || polarity_inverted;
    return ERROR_NONE;
}

// Makes the gauge report charging current as positive. 
// Note: Must be called in CONFIG UPDATE mode.
static error_t fix_current_polarity(Device* device) {
    uint8_t block[BLOCK_SIZE];
    error_t error = read_data_block(device, DATA_CLASS_CC_CAL, DATA_BLOCK_CC_CAL, block);
    if (error != ERROR_NONE) return error;
    if ((block[INDEX_CURRENT_POLARITY] & CC_CAL_POLARITY_BIT) == 0) return ERROR_NONE;

    block[INDEX_CURRENT_POLARITY] = static_cast<uint8_t>(block[INDEX_CURRENT_POLARITY] & ~CC_CAL_POLARITY_BIT);
    return write_data_block(device, DATA_CLASS_CC_CAL, DATA_BLOCK_CC_CAL, block);
}

error_t bq27427_apply_profile(Device* device, const Bq27427Profile* profile) {
    if (!is_profile_valid(profile)) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    UnsealGuard guard(device);
    error_t error = guard.unseal();
    if (error != ERROR_NONE) return error;
    if (guard.was_found_sealed()) delay_millis(UNSEAL_SETTLE_MS);

    LOG_I(TAG, "Writing %u data memory blocks", static_cast<unsigned>(profile->block_count));
    error = write_control(device, CONTROL_SET_CFGUPDATE);
    if (error == ERROR_NONE) {
        delay_millis(CFGUPDATE_SETTLE_MS);
        error = wait_for_config_update_mode(device, true);
    }
    for (size_t i = 0; error == ERROR_NONE && i < profile->block_count; i++) {
        const auto* block = profile->blocks[i];
        error = write_data_block(device, block->data_class, block->block, block->data);
    }
    if (error == ERROR_NONE) {
        error = fix_current_polarity(device);
    }

    // Leaving CONFIG UPDATE mode is attempted even after a failure, so the gauge keeps gauging
    error_t exit_error = write_control(device, CONTROL_SOFT_RESET);
    if (exit_error == ERROR_NONE) {
        exit_error = wait_for_config_update_mode(device, false);
    }

    internal->has_cached_status = false;
    internal->energy_scale = 0;
    if (error == ERROR_NONE && exit_error == ERROR_NONE) {
        internal->energy_scale = profile->energy_scale;
    }
    return error != ERROR_NONE ? error : exit_error;
}

error_t bq27427_wait_ready(Device* device, uint32_t timeout_ms) {
    ScopedLock lock(GET_INTERNAL(device));

    uint32_t waited_ms = 0;
    while (true) {
        uint16_t flags = 0;
        error_t error = read_u16(device, REG_FLAGS, &flags);
        if (error != ERROR_NONE) return error;
        if ((flags & BQ27427_FLAG_ITPOR) == 0) return ERROR_NONE;
        if (waited_ms >= timeout_ms) return ERROR_TIMEOUT;
        delay_millis(READY_POLL_INTERVAL_MS);
        waited_ms += READY_POLL_INTERVAL_MS;
    }
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
