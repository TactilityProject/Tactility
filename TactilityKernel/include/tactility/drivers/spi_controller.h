// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>

struct Device;

/** A peripheral that was added to the bus of an SPI controller. Opaque to callers. */
struct SpiDevice;

/** The index value of SpiDeviceConfig.cs_index for a peripheral whose chip select the caller drives */
#define SPI_CS_NONE (-1)

struct SpiDeviceConfig {
    uint32_t clock_speed_hz;
    /** The SPI mode: 0, 1, 2 or 3 */
    uint8_t mode;
    /**
     * Index into the controller's chip select pins, or SPI_CS_NONE.
     * The controller selects the peripheral for the duration of each transmit() or between acquire_bus() and release_bus().
     */
    int32_t cs_index;
    /**
     * Half-duplex peripheral. The controller may skip the receive phase for write-only transfers
     * (and the platform timing calculation can assume no simultaneous RX), which matters for
     * write-heavy devices like displays.
     */
    bool half_duplex;
};

/**
 * @brief API for SPI controller drivers.
 */
struct SpiControllerApi {
    /**
     * @brief Locks the SPI controller.
     * @param[in] device the SPI controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*lock)(struct Device* device);

    /**
     * @brief Tries to lock the SPI controller.
     * @param[in] device the SPI controller device
     * @param[in] timeout the maximum time to wait for the lock
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*try_lock)(struct Device* device, TickType_t timeout);

    /**
     * @brief Unlocks the SPI controller.
     * @param[in] device the SPI controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*unlock)(struct Device* device);

    /**
     * @brief Adds a peripheral to the bus.
     * @param[in] device the SPI controller device
     * @param[in] config the peripheral's settings
     * @param[out] spi_device the added peripheral
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_OUT_OF_RANGE when the chip select index doesn't exist
     */
    error_t (*add_device)(struct Device* device, const struct SpiDeviceConfig* config, struct SpiDevice** spi_device);

    /**
     * @brief Removes a peripheral from the bus.
     * @param[in] device the SPI controller device
     * @param[in] spi_device a peripheral that was added to this controller
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*remove_device)(struct Device* device, struct SpiDevice* spi_device);

    /**
     * @brief Does a blocking full-duplex transfer.
     * @param[in] device the SPI controller device
     * @param[in] spi_device a peripheral that was added to this controller
     * @param[in] tx_data the bytes to send, or NULL to send zeroes
     * @param[out] rx_data the bytes that are received, or NULL to discard them
     * @param[in] length the number of bytes to transfer
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*transmit)(struct Device* device, struct SpiDevice* spi_device, const void* tx_data, void* rx_data, size_t length);

    /**
     * @brief Takes the bus for a series of transfers and selects the peripheral until release_bus().
     * @param[in] device the SPI controller device
     * @param[in] spi_device a peripheral that was added to this controller
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*acquire_bus)(struct Device* device, struct SpiDevice* spi_device);

    /**
     * @brief Ends a series of transfers and deselects the peripheral.
     * @param[in] device the SPI controller device
     * @param[in] spi_device a peripheral that was added to this controller
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*release_bus)(struct Device* device, struct SpiDevice* spi_device);
};

/**
 * @brief Locks the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_lock(struct Device* device);

/**
 * @brief Tries to lock the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
 * @param[in] timeout the maximum ticks to wait for the lock
 * @retval ERROR_NONE when the operation was successful
 * @retval ERROR_TIMEOUT when the operation timed out
 */
error_t spi_controller_try_lock(struct Device* device, TickType_t timeout);

/**
 * @brief Unlocks the SPI controller using the specified controller.
 * @param[in] device the SPI controller device
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_unlock(struct Device* device);

/**
 * @brief Adds a peripheral to the bus of the SPI controller.
 * @param[in] device the SPI controller device
 * @param[in] config the peripheral's settings
 * @param[out] spi_device the added peripheral
 * @retval ERROR_NONE when the operation was successful
 * @retval ERROR_OUT_OF_RANGE when the chip select index doesn't exist
 */
error_t spi_controller_add_device(struct Device* device, const struct SpiDeviceConfig* config, struct SpiDevice** spi_device);

/**
 * @brief Removes a peripheral from the bus of the SPI controller.
 * @param[in] device the SPI controller device
 * @param[in] spi_device a peripheral that was added to this controller
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_remove_device(struct Device* device, struct SpiDevice* spi_device);

/**
 * @brief Does a blocking full-duplex transfer with a peripheral.
 * The caller holds the controller lock when the bus is shared.
 * @param[in] device the SPI controller device
 * @param[in] spi_device a peripheral that was added to this controller
 * @param[in] tx_data the bytes to send, or NULL to send zeroes
 * @param[out] rx_data the bytes that are received, or NULL to discard them
 * @param[in] length the number of bytes to transfer
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_transmit(struct Device* device, struct SpiDevice* spi_device, const void* tx_data, void* rx_data, size_t length);

/**
 * @brief Takes the bus for a series of transfers and selects the peripheral until spi_controller_release_bus().
 * @param[in] device the SPI controller device
 * @param[in] spi_device a peripheral that was added to this controller
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_acquire_bus(struct Device* device, struct SpiDevice* spi_device);

/**
 * @brief Ends a series of transfers and deselects the peripheral.
 * @param[in] device the SPI controller device
 * @param[in] spi_device a peripheral that was added to this controller
 * @retval ERROR_NONE when the operation was successful
 */
error_t spi_controller_release_bus(struct Device* device, struct SpiDevice* spi_device);

/**
 * @brief Locks device's parent bus when the parent is a SPI controller.
 * @param[in] device the device whose parent may be a SPI controller
 * @retval ERROR_NONE when the operation was successful, or the parent is not a SPI controller
 */
error_t spi_controller_lock_bus_of(struct Device* device);

/**
 * @brief Unlocks a device's parent bus.
 * @param[in] device the device whose parent may be a SPI controller
 */
void spi_controller_unlock_bus_of(struct Device* device);

extern const struct DeviceType SPI_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
