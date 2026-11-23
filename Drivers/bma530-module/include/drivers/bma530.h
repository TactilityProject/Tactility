// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/drivers/imu.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bma530Config {
    /** Address on bus */
    uint8_t address;
    /**
     * Row-major 3x3 rotation matrix
     */
    int32_t mount_matrix[9];
};

/** Accelerometer measurement range. (Values are the ACC_CONF_2 range field) */
enum Bma530Range {
    BMA530_RANGE_2G = 0,
    BMA530_RANGE_4G = 1,
    BMA530_RANGE_8G = 2,
    BMA530_RANGE_16G = 3
};

/** Output data rate (Values are the ACC_CONF_1 ODR field) */
enum Bma530Odr {
    BMA530_ODR_1_5625_HZ = 0x0, // low power mode only
    BMA530_ODR_3_125_HZ = 0x1, // low power mode only
    BMA530_ODR_6_25_HZ = 0x2, // low power mode only
    BMA530_ODR_12_5_HZ = 0x3,
    BMA530_ODR_25_HZ = 0x4,
    BMA530_ODR_50_HZ = 0x5,
    BMA530_ODR_100_HZ = 0x6,
    BMA530_ODR_200_HZ = 0x7,
    BMA530_ODR_400_HZ = 0x8,
    BMA530_ODR_800_HZ = 0x9, // high performance mode only
    BMA530_ODR_1600_HZ = 0xA, // high performance mode only
    BMA530_ODR_3200_HZ = 0xB, // high performance mode only
    BMA530_ODR_6400_HZ = 0xC // high performance mode only
};

/**
 * Filter bandwidth parameter
 * The meaning depends on the power mode: 
 * - in high performance mode it selects an oversampling filter
 * - in low power mode a number of averaged samples.
 */
enum Bma530Bandwidth {
    BMA530_BWP_OSR4_AVG1 = 0,
    BMA530_BWP_OSR2_AVG2 = 1,
    BMA530_BWP_NORM_AVG4 = 2,
    BMA530_BWP_CIC_AVG8 = 3
};

enum Bma530PowerMode {
    /** Duty-cycled measurement, lowest current consumption */
    BMA530_POWER_MODE_LOW_POWER = 0,
    /** Continuous measurement */
    BMA530_POWER_MODE_HIGH_PERFORMANCE = 1
};

/** IIR filter roll-off used in high performance mode */
enum Bma530IirRollOff {
    BMA530_IIR_RO_UNSPECIFIED = 0, // reset value, not documented as a selectable setting though it looks like
    BMA530_IIR_RO_20_DB = 1,
    BMA530_IIR_RO_40_DB = 2,
    BMA530_IIR_RO_60_DB = 3
};

enum Bma530NoiseMode {
    BMA530_NOISE_MODE_LOWER_NOISE = 0,
    /** Lower current, higher noise. **Only** to be used in high performance mode!!! */
    BMA530_NOISE_MODE_LOWER_POWER = 1
};

struct Bma530AccConfig {
    enum Bma530Odr odr;
    enum Bma530Bandwidth bandwidth;
    enum Bma530PowerMode power_mode;
    enum Bma530Range range;
    enum Bma530IirRollOff iir_roll_off;
    enum Bma530NoiseMode noise_mode;
    /** Clear the data-ready interrupt flag automatically after about 1/(2*ODR) */
    bool drdy_auto_clear;
};

/** Raw temperature sampling rate 9values are the TEMP_CONF rate field) */
enum Bma530TemperatureRate {
    BMA530_TEMPERATURE_RATE_1_5625_HZ = 0,
    BMA530_TEMPERATURE_RATE_3_125_HZ = 1,
    BMA530_TEMPERATURE_RATE_6_25_HZ = 2,
    BMA530_TEMPERATURE_RATE_12_5_HZ = 3,
    BMA530_TEMPERATURE_RATE_25_HZ = 4,
    BMA530_TEMPERATURE_RATE_50_HZ = 5,
    BMA530_TEMPERATURE_RATE_100_HZ = 6,
    BMA530_TEMPERATURE_RATE_200_HZ = 7
};

struct Bma530SensorStatus {
    bool acc_data_ready;
    bool temperature_ready;
    bool sensor_ready;
};

/** Hardware interrupt output pins of the sensor */
enum Bma530IntPin {
    BMA530_INT_PIN_1 = 0,
    BMA530_INT_PIN_2 = 1
};

/** Where an interrupt source is routed */
enum Bma530IntLine {
    BMA530_INT_LINE_UNMAPPED = 0,
    BMA530_INT_LINE_INT1 = 1,
    BMA530_INT_LINE_INT2 = 2,
    BMA530_INT_LINE_I3C = 3
};

enum Bma530IntMode {
    BMA530_INT_MODE_OFF = 0,
    /** Level triggered, stays kept until the status flag is cleared */
    BMA530_INT_MODE_LATCHED = 1,
    BMA530_INT_MODE_PULSED_SHORT = 2,
    BMA530_INT_MODE_PULSED_LONG = 3
};

struct Bma530IntPinConfig {
    enum Bma530IntMode mode;
    bool open_drain;
    bool active_high;
};

/** 
 * Interrupt sources
 * Each one can be routed with bma530_map_interrupt()
 */
enum Bma530IntSource {
    BMA530_INT_SOURCE_ACC_DRDY,
    BMA530_INT_SOURCE_FIFO_WATERMARK,
    BMA530_INT_SOURCE_FIFO_FULL,
    BMA530_INT_SOURCE_GENERIC_1,
    BMA530_INT_SOURCE_GENERIC_2,
    BMA530_INT_SOURCE_GENERIC_3,
    BMA530_INT_SOURCE_STEP_DETECTOR,
    BMA530_INT_SOURCE_STEP_COUNTER,
    BMA530_INT_SOURCE_SIGNIFICANT_MOTION,
    BMA530_INT_SOURCE_TILT,
    BMA530_INT_SOURCE_ORIENTATION,
    BMA530_INT_SOURCE_ACC_FOC,
    BMA530_INT_SOURCE_FEATURE_ENGINE_ERROR
};

/** Bit flags returned by bma530_get_int_status() */
#define BMA530_INT_FLAG_ACC_DRDY (1u << 0)
#define BMA530_INT_FLAG_FIFO_WATERMARK (1u << 1)
#define BMA530_INT_FLAG_FIFO_FULL (1u << 2)
#define BMA530_INT_FLAG_GENERIC_1 (1u << 3)
#define BMA530_INT_FLAG_GENERIC_2 (1u << 4)
#define BMA530_INT_FLAG_GENERIC_3 (1u << 5)
#define BMA530_INT_FLAG_STEP_DETECTOR (1u << 6)
#define BMA530_INT_FLAG_STEP_COUNTER (1u << 7)
#define BMA530_INT_FLAG_SIGNIFICANT_MOTION (1u << 8)
#define BMA530_INT_FLAG_TILT (1u << 9)
#define BMA530_INT_FLAG_ORIENTATION (1u << 10)
#define BMA530_INT_FLAG_ACC_FOC (1u << 11)
#define BMA530_INT_FLAG_FEATURE_ENGINE_ERROR (1u << 12)

/** Feature engine features, basically used as a bit mask with bma530_set_enabled_features() */
#define BMA530_FEATURE_GENERIC_1 (1u << 0)
#define BMA530_FEATURE_GENERIC_2 (1u << 1)
#define BMA530_FEATURE_GENERIC_3 (1u << 2)
#define BMA530_FEATURE_STEP_COUNTER (1u << 3)
#define BMA530_FEATURE_SIGNIFICANT_MOTION (1u << 4)
#define BMA530_FEATURE_TILT (1u << 5)
#define BMA530_FEATURE_ORIENTATION (1u << 6)
#define BMA530_FEATURE_ACC_FOC (1u << 7)

/** Orientation detector output, in the sensor frame (matrix not applied) */
enum Bma530Orientation {
    BMA530_ORIENTATION_PORTRAIT_UP = 0,
    BMA530_ORIENTATION_LANDSCAPE_LEFT = 1,
    BMA530_ORIENTATION_PORTRAIT_DOWN = 2,
    BMA530_ORIENTATION_LANDSCAPE_RIGHT = 3
};

enum Bma530Activity {
    BMA530_ACTIVITY_UNKNOWN = 0,
    BMA530_ACTIVITY_STILL = 1,
    BMA530_ACTIVITY_WALKING = 2,
    BMA530_ACTIVITY_RUNNING = 3
};

struct Bma530OrientationConfig {
    /** Detect face up / face down as well as to the in-plane orientation */
    bool face_up_down_enable;
    /** Spread of the detection plane: 0 symmetric, 1 high asymmetric, 2 low asymmetric */
    uint8_t mode;
    /** Blocking mode 0-3, prevents orientation changes during large movements like yeeting the device */
    uint8_t blocking;
    /** Maximum tilt angle for the device to still count as flat (6 bit) */
    uint8_t theta;
    /** Minimum time the device has to stay in the new orientation (5 bit) */
    uint8_t hold_time;
    /** Minimum slope between consecutive samples that blocks an orientation change (like if the device is being shaken or something) */
    uint8_t slope_threshold;
    /** 
     * Acceleration hysteresis for orientation changes
     * (fun word! it means the "dependence of a system's current state on its historical path") 
     */
    uint8_t hysteresis;
};

struct Bma530FeatureOutput {
    /** 24-bit step counter value */
    uint32_t step_count;
    enum Bma530Orientation orientation;
    /** Only valid when face_up_down_enable is set in the orientation configuration (obviously) mmmmmmm*/
    bool face_down;
    enum Bma530Activity activity;
    bool generic_1_active;
    bool generic_2_active;
    bool generic_3_active;
};

/** Offset compensation values, 9 bit signed, in raw sensor LSB. */
#define BMA530_OFFSET_MIN (-256)
#define BMA530_OFFSET_MAX 255

/** Duration of one sensor time tick, in nanoseconds (312.5 us) */
#define BMA530_SENSOR_TIME_TICK_NS 312500u

/**
 * Read accelerometer data only, scaled with the currently configured range.
 * @param[in] device bma530 device
 * @param[out] data Pointer to ImuAccelData structure to store the data, in g
 * @return ERROR_NONE on success
 */
error_t bma530_read_accel(struct Device* device, struct ImuAccelData* data);

/**
 * Read the unscaled accelerometer output in sensor frame, without applying the mount matrix.
 * @param[in] device bma530 device
 * @param[out] raw X, Y and Z as signed 16-bit values
 * @return ERROR_NONE on success
 */
error_t bma530_read_accel_raw(struct Device* device, int16_t raw[3]);

/**
 * Read the on-chip temperature (1 K per LSB, 0 equals 23 degrees C).
 * @param[in] device bma530 device
 * @param[out] temperature_c Pointer to store the temperature, in degrees C
 * @return ERROR_NONE on success
 */
error_t bma530_read_temperature(struct Device* device, float* temperature_c);

/**
 * Read the free running sensor time counter
 * @param[in] device bma530 device
 * @param[out] ticks 24-bit counter value, one tick is BMA530_SENSOR_TIME_TICK_NS
 * @return ERROR_NONE on success
 */
error_t bma530_read_sensor_time(struct Device* device, uint32_t* ticks);

/**
 * Read the accelerometer configuration.
 * @return ERROR_NONE on success
 */
error_t bma530_get_acc_config(struct Device* device, struct Bma530AccConfig* config);

/**
 * Apply an accelerometer configuration. The sensor is disabled while the configuration is written
 * and enabled again afterwards. The new range is used for scaling by bma530_read_accel().
 * @retval ERROR_INVALID_ARGUMENT if a field is out of range or the sensor rejects the combination
 * (for example a high-performance-only ODR in low power mode)
 * @retval ERROR_NONE on success
 */
error_t bma530_set_acc_config(struct Device* device, const struct Bma530AccConfig* config);

/** Enable or disable the accelerometer and temperature sensor. */
error_t bma530_set_enabled(struct Device* device, bool enabled);

/**
 * Enter or leave suspend. Register contents survive suspend, but only the chip ID and suspend
 * registers are accessible while suspended, so the other calls fail until suspend is left.
 */
error_t bma530_set_suspended(struct Device* device, bool suspended);

/**
 * Restart the sensor through the soft-reset command, then verify the chip ID and re-apply the
 * driver's default configuration. All settings made through this API are lost.
 * @retval ERROR_RESOURCE if the chip does not identify as a BMA530 afterwards (identity crisis lolll)
 */
error_t bma530_soft_reset(struct Device* device);

/** Set how often the temperature is sampled. */
error_t bma530_set_temperature_rate(struct Device* device, enum Bma530TemperatureRate rate);

/** Read the data-ready and sensor-ready flags. */
error_t bma530_get_sensor_status(struct Device* device, struct Bma530SensorStatus* status);

/** Clear the data-ready flags that are set in `status` (the sensor_ready field is ignored). */
error_t bma530_clear_sensor_status(struct Device* device, const struct Bma530SensorStatus* status);

/**
 * Read the offset compensation applied to the output.
 * @param[out] offsets X, Y and Z offset, within BMA530_OFFSET_MIN and BMA530_OFFSET_MAX
 */
error_t bma530_get_acc_offsets(struct Device* device, int16_t offsets[3]);

/**
 * Write the offset compensation applied to the output.
 * @retval ERROR_OUT_OF_RANGE if an offset does not fit in 9 bit signed
 */
error_t bma530_set_acc_offsets(struct Device* device, const int16_t offsets[3]);

/** Configure the electrical behaviour of one of the interrupt output pinss */
error_t bma530_set_int_pin_config(struct Device* device, enum Bma530IntPin pin, const struct Bma530IntPinConfig* config);

/** Read the electrical configuration of one of the interrupt output pins. */
error_t bma530_get_int_pin_config(struct Device* device, enum Bma530IntPin pin, struct Bma530IntPinConfig* config);

/** Route an interrupt source to a pin (or unmap it) but other sources keep their routing. */
error_t bma530_map_interrupt(struct Device* device, enum Bma530IntSource source, enum Bma530IntLine line);

/**
 * Read the pending interrupt flags of one line
 * @param[out] flags combination of BMA530_INT_FLAG_* values
 */
error_t bma530_get_int_status(struct Device* device, enum Bma530IntLine line, uint16_t* flags);

/** Clear pending interrupt flags of one line (BMA530_INT_FLAG_* values). */
error_t bma530_clear_int_status(struct Device* device, enum Bma530IntLine line, uint16_t flags);

/** Read the orientation detector configuration. */
error_t bma530_get_orientation_config(struct Device* device, struct Bma530OrientationConfig* config);

/**
 * Write the orientation detector configuration thing
 * @retval ERROR_INVALID_ARGUMENT if a field exceeds its bit width
 */
error_t bma530_set_orientation_config(struct Device* device, const struct Bma530OrientationConfig* config);

/**
 * Select the feature engine features that run, replacing the previous selection.
 * @param[in] features combination of BMA530_FEATURE_* values. The generic interrupts, significant
 * motion, tilt and offset compensation features can be enabled here, but this driver has no
 * configuration calls for them (yet :P)
 */
error_t bma530_set_enabled_features(struct Device* device, uint8_t features);

/** Read the results of the feature output which includes step count, orientation and activity */
error_t bma530_get_feature_output(struct Device* device, struct Bma530FeatureOutput* output);

#ifdef __cplusplus
}
#endif
