#include <Tactility/hal/gps/GpsDevice.h>
#include <Tactility/hal/gps/GpsInit.h>
#include <Tactility/hal/gps/Probe.h>
#include <Tactility/Logger.h>

#include <tactility/device.h>
#include <tactility/drivers/uart_controller.h>

#include <cstring>
#include <minmea.h>

namespace tt::hal::gps {

constexpr uint32_t GPS_UART_BUFFER_SIZE = 256;

static const auto LOGGER = Logger("GpsDevice");

int32_t GpsDevice::threadMain() {
    uint8_t buffer[GPS_UART_BUFFER_SIZE];

    auto* uart = device_find_by_name(configuration.uartName);
    if (uart == nullptr) {
        LOGGER.error("Failed to find UART {}", configuration.uartName);
        return -1;
    }

    struct UartConfig uartConfig = {
        .baud_rate = configuration.baudRate,
        .data_bits = UART_CONTROLLER_DATA_8_BITS,
        .parity = UART_CONTROLLER_PARITY_DISABLE,
        .stop_bits = UART_CONTROLLER_STOP_BITS_1
    };

    error_t error = uart_controller_set_config(uart, &uartConfig);
    if (error != ERROR_NONE) {
        LOGGER.error("Failed to configure UART {}: {}", configuration.uartName, error_to_string(error));
        return -1;
    }

    error = uart_controller_open(uart);
    if (error != ERROR_NONE) {
        LOGGER.error("Failed to open UART {}: {}", configuration.uartName, error_to_string(error));
        return -1;
    }

    GpsModel model = configuration.model;
    if (model == GpsModel::Unknown) {
        model = probe(uart);
        if (model == GpsModel::Unknown) {
            LOGGER.error("Probe failed");
            setState(State::Error);
            return -1;
        }
    }
    mutex.lock();
    this->model = model;
    mutex.unlock();

    if (!init(uart, model)) {
        LOGGER.error("Init failed");
        setState(State::Error);
        return -1;
    }

    setState(State::On);

    // Reference: https://gpsd.gitlab.io/gpsd/NMEA.html
    while (!isThreadInterrupted()) {
        size_t bytes_read = 0;
        uart_controller_read_until(uart, buffer, GPS_UART_BUFFER_SIZE, '\n', true, &bytes_read, 100 / portTICK_PERIOD_MS);

        // Thread might've been interrupted in the meanwhile
        if (isThreadInterrupted()) {
            break;
        }

        if (bytes_read > 0U) {

            LOGGER.info("[{}] {}", bytes_read, reinterpret_cast<const char*>(buffer));

            switch (minmea_sentence_id((char*)buffer, false)) {
                case MINMEA_SENTENCE_RMC:
                    minmea_sentence_rmc rmc_frame;
                    if (minmea_parse_rmc(&rmc_frame, (char*)buffer)) {
                        mutex.lock();
                        for (auto& subscription : rmcSubscriptions) {
                            (*subscription.onData)(getId(), rmc_frame);
                        }
                        mutex.unlock();
                        if (LOGGER.isLoggingDebug()) {
                            LOGGER.debug("RMC {} lat, {} lon, {} m/s", minmea_tocoord(&rmc_frame.latitude), minmea_tocoord(&rmc_frame.longitude), minmea_tofloat(&rmc_frame.speed));
                        }
                    } else {
                        LOGGER.error("RMC parse error: {}", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                case MINMEA_SENTENCE_GGA:
                    minmea_sentence_gga gga_frame;
                    if (minmea_parse_gga(&gga_frame, (char*)buffer)) {
                        mutex.lock();
                        for (auto& subscription : ggaSubscriptions) {
                            (*subscription.onData)(getId(), gga_frame);
                        }
                        mutex.unlock();
                        if (LOGGER.isLoggingDebug()) {
                            LOGGER.debug("GGA {} lat, {} lon", minmea_tocoord(&gga_frame.latitude), minmea_tocoord(&gga_frame.longitude));
                        }
                    } else {
                        LOGGER.error("GGA parse error: {}", reinterpret_cast<const char*>(buffer));
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (uart_controller_close(uart) != ERROR_NONE) {
        LOGGER.warn("Failed to stop UART {}", configuration.uartName);
    }

    return 0;
}

bool GpsDevice::start() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (thread != nullptr && thread->getState() != Thread::State::Stopped) {
        LOGGER.warn("Already started");
        return true;
    }

    threadInterrupted = false;

    LOGGER.info("Starting thread");
    setState(State::PendingOn);

    thread = std::make_unique<Thread>(
        "gps",
        4096,
        [this]() {
            return this->threadMain();
        }
    );
    thread->setPriority(tt::Thread::Priority::High);
    thread->start();

    LOGGER.info("Starting finished");
    return true;
}

bool GpsDevice::stop() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    setState(State::PendingOff);

    if (thread != nullptr) {
        threadInterrupted = true;

        // Detach thread, it will auto-delete when leaving the current scope
        auto old_thread = std::move(thread);

        if (old_thread->getState() != Thread::State::Stopped) {
            // Unlock so thread can lock
            lock.unlock();
            // Wait for thread to finish
            old_thread->join();
            // Re-lock to continue logic below
            lock.lock();
        }
    }

    setState(State::Off);

    return true;
}

bool GpsDevice::isThreadInterrupted() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return threadInterrupted;
}

GpsModel GpsDevice::getModel() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return model; // Make copy because of thread safety
}

GpsDevice::State GpsDevice::getState() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return state; // Make copy because of thread safety
}

void GpsDevice::setState(State newState) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    state = newState;
}

} // namespace tt::hal::gps
