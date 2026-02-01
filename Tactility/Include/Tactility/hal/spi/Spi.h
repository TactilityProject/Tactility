#pragma once

#include "SpiCompat.h"

#include <Tactility/Lock.h>
#include <Tactility/freertoscompat/RTOS.h>

namespace tt::hal::spi {

constexpr TickType_t defaultTimeout = 10 / portTICK_PERIOD_MS;

enum class InitMode {
    ByTactility, // Tactility will initialize it in the correct bootup phase
    ByExternal, // The device is already initialized and Tactility should assume it works
    Disabled // Not initialized by default
};

struct Configuration {
    spi_host_device_t device;
    spi_common_dma_t dma;
    spi_bus_config_t config;
    /** Whether this bus should be initialized when device starts up */
    InitMode initMode;
    /** Whether configuration can be changed. */
    bool isMutable;
    /** Optional custom lock - otherwise creates one internally */
    std::shared_ptr<Lock> lock;
};

enum class Status {
    Started,
    Stopped,
    Unknown
};

/** Start communications */
bool start(spi_host_device_t device);

/** Stop communications */
bool stop(spi_host_device_t device);

/** @return true if communications were started successfully */
bool isStarted(spi_host_device_t device);

/**
 * Return the lock for the specified SPI device. Never returns nullptr.
 * @return the lock that represents the specified device. Can be used with third party SPI implementations or native API calls (e.g. ESP-IDF).
 */
std::shared_ptr<Lock> getLock(spi_host_device_t device);

} // namespace tt::hal::spi
