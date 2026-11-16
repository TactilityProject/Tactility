// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

/** Size of one data memory block in bytes */
#define BQ27427_DATA_BLOCK_SIZE 32

struct Bq27427Config {
    /** Address on bus */
    uint8_t address;
};

/** Bits of Bq27427Status::flags (the Flags() register: low byte first, then high byte) */
enum Bq27427Flag {
    BQ27427_FLAG_DSG = (1 << 0), // discharging detected
    BQ27427_FLAG_SOCF = (1 << 1), // final discharge warning
    BQ27427_FLAG_SOC1 = (1 << 2), // low battery warning
    BQ27427_FLAG_BAT_DET = (1 << 3), // battery detected: gauge predictions are not valid without it
    BQ27427_FLAG_CFGUPMODE = (1 << 4), // in CONFIG UPDATE mode
    BQ27427_FLAG_ITPOR = (1 << 5), // power-on reset or RESET subcommand occurred
    BQ27427_FLAG_DOD_CORRECT = (1 << 6),
    BQ27427_FLAG_OCVTAKEN = (1 << 7),
    BQ27427_FLAG_CHG = (1 << 8), // fast charging allowed
    BQ27427_FLAG_FC = (1 << 9), // full charge detected
    BQ27427_FLAG_UT = (1 << 14), // under-temperature
    BQ27427_FLAG_OT = (1 << 15) // over-temperature
};

/** Bits of the CONTROL_STATUS word */
enum Bq27427ControlStatusBit {
    BQ27427_CONTROL_STATUS_INITCOMP = (1 << 7),
    BQ27427_CONTROL_STATUS_SLEEP = (1 << 4),
    BQ27427_CONTROL_STATUS_LDMD = (1 << 3),
    BQ27427_CONTROL_STATUS_RUP_DIS = (1 << 2),
    BQ27427_CONTROL_STATUS_VOK = (1 << 1),
    BQ27427_CONTROL_STATUS_CHEMCHANGE = (1 << 0),
    BQ27427_CONTROL_STATUS_SHUTDOWNEN = (1 << 15),
    BQ27427_CONTROL_STATUS_WDRESET = (1 << 14),
    BQ27427_CONTROL_STATUS_SS = (1 << 13), // sealed
    BQ27427_CONTROL_STATUS_CALMODE = (1 << 12),
    BQ27427_CONTROL_STATUS_CCA = (1 << 11),
    BQ27427_CONTROL_STATUS_BCA = (1 << 10),
    BQ27427_CONTROL_STATUS_QMAX_UP = (1 << 9),
    BQ27427_CONTROL_STATUS_RES_UP = (1 << 8)
};

/** Read-only Control() subcommands that return a 16-bit value */
enum Bq27427ControlRead {
    BQ27427_CONTROL_READ_STATUS = 0x0000,
    BQ27427_CONTROL_READ_DEVICE_TYPE = 0x0001, // 0x0427
    BQ27427_CONTROL_READ_FW_VERSION = 0x0002,
    BQ27427_CONTROL_READ_DM_CODE = 0x0004,
    BQ27427_CONTROL_READ_PREV_MACWRITE = 0x0007,
    BQ27427_CONTROL_READ_CHEM_ID = 0x0008
};

/** Control() subcommands that are available while sealed and have no return value */
enum Bq27427ControlCommand {
    /** Forces Flags() BAT_DET set. Only effective when OpConfig BIE is 0. */
    BQ27427_CONTROL_COMMAND_BAT_INSERT = 0x000C,
    /** Forces Flags() BAT_DET clear. Only effective when OpConfig BIE is 0. */
    BQ27427_CONTROL_COMMAND_BAT_REMOVE = 0x000D,
    /** Synchronizes the smoothed remaining/full charge capacity with the true values */
    BQ27427_CONTROL_COMMAND_SMOOTH_SYNC = 0x0019,
    /** Pulses GPOUT for about 1 ms */
    BQ27427_CONTROL_COMMAND_PULSE_SOC_INT = 0x0023
};

/**
 * A "snapshot" of all standard commands of the gauge.
 * Capacities are already multiplied by the Design Energy Scale stored in the gauge, so they are always in mAh.
 */
struct Bq27427Status {
    /** Design Energy Scale (1 or 10) that was applied to the capacity fields */
    uint8_t energy_scale;
    /** Temperature(), in 0.1 K */
    uint16_t temperature_decikelvin;
    /** Voltage(), in mV */
    uint16_t voltage_mv;
    /** Flags(), see Bq27427Flag */
    uint16_t flags;
    /** NominalAvailableCapacity(): uncompensated (below C/20 load) remaining capacity, in mAh */
    uint32_t nominal_available_capacity_mah;
    /** FullAvailableCapacity(): uncompensated (below C/20 load) full capacity, in mAh */
    uint32_t full_available_capacity_mah;
    /** RemainingCapacity(): compensated for load and temperature, in mAh */
    uint32_t remaining_capacity_mah;
    /** FullChargeCapacity(): compensated for load and temperature, in mAh */
    uint32_t full_charge_capacity_mah;
    /** AverageCurrent(), in mA: positive while charging, negative while discharging */
    int16_t average_current_ma;
    /** AveragePower(), in mW: positive while charging, negative while discharging */
    int16_t average_power_mw;
    /** StateOfCharge(), in % */
    uint16_t state_of_charge_percent;
    /** InternalTemperature(), in 0.1 K */
    uint16_t internal_temperature_decikelvin;
    /** StateOfHealth(), in % */
    uint8_t state_of_health_percent;
    /** RemainingCapacityUnfiltered(), in mAh */
    uint32_t remaining_capacity_unfiltered_mah;
    /** RemainingCapacityFiltered(), in mAh */
    uint32_t remaining_capacity_filtered_mah;
    /** FullChargeCapacityUnfiltered(), in mAh */
    uint32_t full_charge_capacity_unfiltered_mah;
    /** FullChargeCapacityFiltered(), in mAh */
    uint32_t full_charge_capacity_filtered_mah;
    /** StateOfChargeUnfiltered(), in % */
    uint16_t state_of_charge_unfiltered_percent;
};

/** One 32-byte block of the gauge's data memory */
struct Bq27427DataBlock {
    /** Subclass ID, see the data memory tables of the technical reference manual */
    uint8_t data_class;
    /** Block number within the subclass: the offset of a parameter divided by 32 */
    uint8_t block;
    /** The block content. Its checksum is derived by the driver. */
    uint8_t data[BQ27427_DATA_BLOCK_SIZE];
};

/** A set of data memory blocks (a "golden image") that configures the gauge for one particular cell */
struct Bq27427Profile {
    /** The blocks to write, in order */
    const struct Bq27427DataBlock* const* blocks;
    /** Number of entries in blocks */
    size_t block_count;
    /**
     * The Design Energy Scale (1..10) that the blocks program. It is used to tell whether the gauge
     * already holds this profile. The technical reference manual recommends 10 for packs over 6 Ah.
     */
    uint8_t energy_scale;
};

/**
 * Reads all standard commands of the gauge.
 * The gauge's watchdog may reset it when polled more than twice per second, so results are cached for one second.
 * @param[in] device the bq27427 device
 * @param[out] status the snapshot
 * @retval ERROR_INVALID_STATE when the gauge returned values outside of the physically possible range
 */
error_t bq27427_get_status(struct Device* device, struct Bq27427Status* status);

/**
 * Reads the result of a read-only Control() subcommand.
 * @param[in] device the bq27427 device
 * @param[in] subcommand the subcommand to read
 * @param[out] value the 16-bit result
 * @retval ERROR_INVALID_ARGUMENT when the subcommand is not a Bq27427ControlRead value
 */
error_t bq27427_read_control(struct Device* device, enum Bq27427ControlRead subcommand, uint16_t* value);

/**
 * Sends a Control() subcommand that has no result.
 * @param[in] device the bq27427 device
 * @param[in] command the command to send
 * @retval ERROR_INVALID_ARGUMENT when the command is not a Bq27427ControlCommand value
 */
error_t bq27427_send_control(struct Device* device, enum Bq27427ControlCommand command);

/**
 * Tells whether the gauge has to be configured with a profile. That is the case when the gauge lost its
 * RAM configuration (Flags() ITPOR), when its Design Energy Scale differs from the profile's, or when it
 * still reports current with the inverted polarity (charging negative).
 * @param[in] device the bq27427 device
 * @param[in] profile the profile that is intended to be used
 * @param[out] needed true when bq27427_apply_profile() has to run
 * @retval ERROR_NOT_ALLOWED when the gauge is sealed and none of the known keys unseal it
 */
error_t bq27427_profile_needed(struct Device* device, const struct Bq27427Profile* profile, bool* needed);

/**
 * Writes all blocks of the profile to the gauge's data memory and makes it report charging current as positive
 * The gauge is unsealed, put in CONFIG UPDATE mode, written, soft reset and put back in the access mode it had.
 * 
 * @note This blocks for several seconds.
 * @param[in] device the bq27427 device
 * @param[in] profile the profile to write
 * @retval ERROR_INVALID_ARGUMENT when the profile is malformed
 * @retval ERROR_NOT_ALLOWED when the gauge is sealed and none of the known keys unseal it
 * @retval ERROR_RESOURCE when a block was not accepted by the gauge
 */
error_t bq27427_apply_profile(struct Device* device, const struct Bq27427Profile* profile);

/**
 * Waits until the gauge finished initializing after a reset.
 *
 * @param[in] device the bq27427 device
 * @param[in] timeout_ms how long to wait
 * @retval ERROR_TIMEOUT when the gauge is still initializing
 */
error_t bq27427_wait_ready(struct Device* device, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
