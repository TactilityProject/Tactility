// SPDX-License-Identifier: Apache-2.0

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/posix_uart.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/log.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define TAG "posix_uart"

namespace {

struct PosixUartInternal {
    Mutex mutex {};
    UartConfig config {};
    bool config_set = false;
    int fd = -1;

    PosixUartInternal() {
        mutex_construct(&mutex);
    }

    ~PosixUartInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((const PosixUartConfig*)device->config)
#define GET_DATA(device) ((PosixUartInternal*)device_get_driver_data(device))

#define lock(data) mutex_lock(&data->mutex)
#define unlock(data) mutex_unlock(&data->mutex)

using Clock = std::chrono::steady_clock;

Clock::time_point to_deadline(TickType_t timeout) {
    if (timeout == portMAX_DELAY) return Clock::time_point::max();
    return Clock::now() + std::chrono::milliseconds((uint64_t)timeout * portTICK_PERIOD_MS);
}

int remaining_poll_ms(Clock::time_point deadline) {
    if (deadline == Clock::time_point::max()) return -1;
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return remaining > 0 ? (int)remaining : 0;
}

/** @return true when the fd became ready before the deadline */
bool wait_for(int fd, short events, Clock::time_point deadline) {
    while (true) {
        pollfd pfd = { .fd = fd, .events = events, .revents = 0 };
        int result = poll(&pfd, 1, remaining_poll_ms(deadline));
        if (result > 0) return true;
        if (result == 0) return false;
        if (errno != EINTR) return false;
    }
}

bool to_speed(uint32_t baud_rate, speed_t* speed) {
    switch (baud_rate) {
        case 1200: *speed = B1200; return true;
        case 2400: *speed = B2400; return true;
        case 4800: *speed = B4800; return true;
        case 9600: *speed = B9600; return true;
        case 19200: *speed = B19200; return true;
        case 38400: *speed = B38400; return true;
        case 57600: *speed = B57600; return true;
        case 115200: *speed = B115200; return true;
        case 230400: *speed = B230400; return true;
#ifdef B460800
        case 460800: *speed = B460800; return true;
#endif
#ifdef B921600
        case 921600: *speed = B921600; return true;
#endif
#ifdef B1000000
        case 1000000: *speed = B1000000; return true;
#endif
#ifdef B2000000
        case 2000000: *speed = B2000000; return true;
#endif
        default: return false;
    }
}

error_t apply_termios(int fd, const UartConfig& config) {
    speed_t speed;
    if (!to_speed(config.baud_rate, &speed)) return ERROR_NOT_SUPPORTED;
    if (config.stop_bits == UART_CONTROLLER_STOP_BITS_1_5) return ERROR_NOT_SUPPORTED;

    termios tty {};
    if (tcgetattr(fd, &tty) != 0) return ERROR_RESOURCE;

    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cflag &= ~CSIZE;
    switch (config.data_bits) {
        case UART_CONTROLLER_DATA_5_BITS: tty.c_cflag |= CS5; break;
        case UART_CONTROLLER_DATA_6_BITS: tty.c_cflag |= CS6; break;
        case UART_CONTROLLER_DATA_7_BITS: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }

    switch (config.parity) {
        case UART_CONTROLLER_PARITY_EVEN: tty.c_cflag |= PARENB; tty.c_cflag &= ~PARODD; break;
        case UART_CONTROLLER_PARITY_ODD: tty.c_cflag |= PARENB | PARODD; break;
        default: tty.c_cflag &= ~PARENB; break;
    }

    if (config.stop_bits == UART_CONTROLLER_STOP_BITS_2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) return ERROR_NOT_SUPPORTED;
    if (tcsetattr(fd, TCSANOW, &tty) != 0) return ERROR_RESOURCE;
    tcflush(fd, TCIOFLUSH);
    return ERROR_NONE;
}

error_t read_locked(PosixUartInternal* data, uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    auto deadline = to_deadline(timeout);
    size_t total = 0;
    while (total < buffer_size) {
        ssize_t result = read(data->fd, buffer + total, buffer_size - total);
        if (result > 0) {
            total += result;
        } else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return ERROR_RESOURCE;
        } else if (!wait_for(data->fd, POLLIN, deadline)) {
            return ERROR_TIMEOUT;
        }
    }
    return ERROR_NONE;
}

error_t write_locked(PosixUartInternal* data, const uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    auto deadline = to_deadline(timeout);
    size_t total = 0;
    while (total < buffer_size) {
        ssize_t result = write(data->fd, buffer + total, buffer_size - total);
        if (result > 0) {
            total += result;
        } else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return ERROR_RESOURCE;
        } else if (!wait_for(data->fd, POLLOUT, deadline)) {
            return ERROR_TIMEOUT;
        }
    }
    return ERROR_NONE;
}

// ---- UartControllerApi ----

error_t read_bytes(Device* device, uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd < 0) {
        unlock(data);
        return ERROR_INVALID_STATE;
    }
    error_t error = read_locked(data, buffer, buffer_size, timeout);
    unlock(data);
    return error;
}

error_t read_byte(Device* device, uint8_t* out, TickType_t timeout) {
    return read_bytes(device, out, 1, timeout);
}

error_t write_bytes(Device* device, const uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd < 0) {
        unlock(data);
        return ERROR_INVALID_STATE;
    }
    error_t error = write_locked(data, buffer, buffer_size, timeout);
    unlock(data);
    return error;
}

error_t write_byte(Device* device, uint8_t out, TickType_t timeout) {
    return write_bytes(device, &out, 1, timeout);
}

error_t get_available(Device* device, size_t* available) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd < 0) {
        unlock(data);
        return ERROR_INVALID_STATE;
    }
    int count = 0;
    int result = ioctl(data->fd, FIONREAD, &count);
    unlock(data);
    if (result != 0) return ERROR_RESOURCE;
    *available = (size_t)count;
    return ERROR_NONE;
}

error_t set_config(Device* device, const UartConfig* config) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd >= 0) {
        unlock(data);
        return ERROR_INVALID_STATE;
    }
    data->config = *config;
    data->config_set = true;
    unlock(data);
    return ERROR_NONE;
}

error_t get_config(Device* device, UartConfig* config) {
    auto* data = GET_DATA(device);
    lock(data);
    if (!data->config_set) {
        unlock(data);
        return ERROR_RESOURCE;
    }
    *config = data->config;
    unlock(data);
    return ERROR_NONE;
}

error_t open_device(Device* device) {
    auto* data = GET_DATA(device);
    const char* path = GET_CONFIG(device)->path;

    lock(data);
    if (data->fd >= 0) {
        unlock(data);
        LOG_W(TAG, "%s is already open", device->name);
        return ERROR_INVALID_STATE;
    }
    if (!data->config_set) {
        unlock(data);
        LOG_E(TAG, "%s open failed: config not set", device->name);
        return ERROR_INVALID_STATE;
    }

    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        unlock(data);
        LOG_E(TAG, "%s failed to open %s: %s", device->name, path, strerror(errno));
        return ERROR_RESOURCE;
    }

    error_t error = apply_termios(fd, data->config);
    if (error != ERROR_NONE) {
        close(fd);
        unlock(data);
        LOG_E(TAG, "%s failed to configure %s: %s", device->name, path, error_to_string(error));
        return error;
    }

    data->fd = fd;
    unlock(data);
    return ERROR_NONE;
}

error_t close_device(Device* device) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd < 0) {
        unlock(data);
        LOG_W(TAG, "%s is already closed", device->name);
        return ERROR_INVALID_STATE;
    }
    close(data->fd);
    data->fd = -1;
    unlock(data);
    return ERROR_NONE;
}

bool is_open(Device* device) {
    auto* data = GET_DATA(device);
    lock(data);
    bool status = data->fd >= 0;
    unlock(data);
    return status;
}

error_t flush_input(Device* device) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd < 0) {
        unlock(data);
        return ERROR_INVALID_STATE;
    }
    int result = tcflush(data->fd, TCIFLUSH);
    unlock(data);
    return result == 0 ? ERROR_NONE : ERROR_RESOURCE;
}

const UartControllerApi posix_uart_api = {
    .read_byte = read_byte,
    .write_byte = write_byte,
    .write_bytes = write_bytes,
    .read_bytes = read_bytes,
    .get_available = get_available,
    .set_config = set_config,
    .get_config = get_config,
    .open = open_device,
    .close = close_device,
    .is_open = is_open,
    .flush_input = flush_input
};

// ---- Driver lifecycle ----

error_t start_device(Device* device) {
    auto* data = new(std::nothrow) PosixUartInternal();
    if (data == nullptr) return ERROR_OUT_OF_MEMORY;
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

error_t stop_device(Device* device) {
    auto* data = GET_DATA(device);
    lock(data);
    if (data->fd >= 0) {
        unlock(data);
        return ERROR_INVALID_STATE;
    }
    unlock(data);
    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

} // namespace

extern "C" {

Driver posix_uart_driver = {
    .name = "posix_uart",
    .compatible = (const char*[]) { "posix,uart", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .probe = nullptr,
    .api = (const void*)&posix_uart_api,
    .device_type = &UART_CONTROLLER_TYPE,
    .internal = nullptr
};

} // extern "C"
