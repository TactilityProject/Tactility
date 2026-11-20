// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/touchbar.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

/** 
 * Number of 16-bit words in each of the sensor setup blocks of the devicetree configuration 
 */
#define IQS323_SENSOR_SETUP_WORDS 30
#define IQS323_CHANNEL_SETUP_WORDS 12
#define IQS323_SLIDER_SETUP_WORDS 9
#define IQS323_GESTURE_SETUP_WORDS 7
#define IQS323_FILTER_SETUP_WORDS 5
#define IQS323_TIMING_SETUP_WORDS 5
#define IQS323_GENERAL_SETUP_WORDS 5

/** Most words that iqs323_read_registers() and iqs323_write_registers() move in a single transaction */
#define IQS323_MAX_TRANSFER_WORDS 32

struct Iqs323Config {
    /** Address on bus */
    uint8_t address;
    struct GpioPinSpec pin_ready;
    /** Power mode to run in, a value of enum Iqs323PowerMode */
    uint8_t power_mode;
    const uint16_t* sensor_setup;
    uint32_t sensor_setup_length;
    const uint16_t* channel_setup;
    uint32_t channel_setup_length;
    const uint16_t* slider_setup;
    uint32_t slider_setup_length;
    const uint16_t* gesture_setup;
    uint32_t gesture_setup_length;
    const uint16_t* filter_setup;
    uint32_t filter_setup_length;
    const uint16_t* timing_setup;
    uint32_t timing_setup_length;
    const uint16_t* general_setup;
    uint32_t general_setup_length;
};

/** Power modes (values are the System Control power mode field) */
enum Iqs323PowerMode {
    IQS323_POWER_MODE_NORMAL = 0,
    IQS323_POWER_MODE_LOW_POWER = 1,
    IQS323_POWER_MODE_ULTRA_LOW_POWER = 2,
    IQS323_POWER_MODE_HALT = 3,
    /** The chip steps down through the power modes by itself when nothing is touched */
    IQS323_POWER_MODE_AUTO = 4,
    /** Like IQS323_POWER_MODE_AUTO, but never enters ultra low power */
    IQS323_POWER_MODE_AUTO_NO_ULP = 5
};

struct Iqs323Info {
    /** 
     * 1106 for the order codes with Release UI (00x) 
     * 1462 for the ones with Movement UI (A0x) 
     */
    uint16_t product_number;
    uint8_t major_version;
    uint8_t minor_version;
    /** The power mode that the chip was in when it last reported though not the automatic ones */
    enum Iqs323PowerMode current_power_mode;
    /** The last calibration did not reach its target */
    bool ati_error;
};

/**
 * Read 16-bit registers of the memory map. 
 * The chip only answers in a communication window, which this call forces open when needed (trust me it works). 
 * 
 * @param[in] device iqs323 device
 * @param[in] reg first register address
 * @param[out] words destination
 * @param[in] count number of words, at most IQS323_MAX_TRANSFER_WORDS
 * @retval ERROR_RESOURCE if the chip did not open a window or answered 0xEE (invalid) for all data
 */
error_t iqs323_read_registers(struct Device* device, uint8_t reg, uint16_t* words, size_t count);

/**
 * Write 16-bit registers of the memory map. 
 * @note This bypasses the driver's bookkeeping, so the System Control register (0xC0) should be 
 * written through the dedicated calls instead.
 * @param[in] device iqs323 device
 * @param[in] reg first register address
 * @param[in] words data
 * @param[in] count number of words, at most IQS323_MAX_TRANSFER_WORDS
 */
error_t iqs323_write_registers(struct Device* device, uint8_t reg, const uint16_t* words, size_t count);

/**
 * Read the version of the chip and the status that it last reported
 * @retval ERROR_INVALID_STATE if the chip has not reported yet
 */
error_t iqs323_get_info(struct Device* device, struct Iqs323Info* info);

/** Change the power mode. The new mode also applies after the chip is reset. */
error_t iqs323_set_power_mode(struct Device* device, enum Iqs323PowerMode mode);

/** 
 * restart the long term averages from the current counts. 
 * @note Use it when the bar is stuck as touched. 
 * */
error_t iqs323_reseed(struct Device* device);

/**
 * Report on every cycle instead of only on events. 
 * Use it for a short time when you want it to be fast as fluff (BOIIIII)
 * The setting is kept when the chip is reset
 */
error_t iqs323_set_streaming(struct Device* device, bool enabled);

/**
 * Recalibrate all channels (re-ATI) and wait until the chip is done (has retries)
 */
error_t iqs323_recalibrate(struct Device* device);

/** Reset the chip through its command register. The driver loads the configuration again afterwards. */
error_t iqs323_soft_reset(struct Device* device);

/**
 * Reset the chip by pulling the RDY/MCLR line low
 * Note that this also recovers a chip which does not respond on I2C...
 */
error_t iqs323_hardware_reset(struct Device* device);

#ifdef __cplusplus
}
#endif
