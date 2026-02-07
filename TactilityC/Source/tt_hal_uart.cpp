#include "tt_hal_uart.h"

#include <cstring>

extern "C" {

size_t tt_hal_uart_get_count() {
    return 0;
}

bool tt_hal_uart_get_name(size_t index, char* name, size_t nameSizeLimit) {
    return false;
}

UartHandle tt_hal_uart_alloc(size_t index) {
    return nullptr;
}

void tt_hal_uart_free(UartHandle handle) {
}

bool tt_hal_uart_start(UartHandle handle) {
    return false;
}

bool tt_hal_uart_is_started(UartHandle handle) {
    return false;
}

bool tt_hal_uart_stop(UartHandle handle) {
    return false;
}

size_t tt_hal_uart_read_bytes(UartHandle handle, char* buffer, size_t bufferSize, TickType_t timeout) {
    return 0;
}

bool tt_hal_uart_read_byte(UartHandle handle, char* output, TickType_t timeout) {
    return false;
}

size_t tt_hal_uart_write_bytes(UartHandle handle, const char* buffer, size_t bufferSize, TickType_t timeout) {
    return 0;
}

size_t tt_hal_uart_available(UartHandle handle) {
    return 0;
}

bool tt_hal_uart_set_baud_rate(UartHandle handle, size_t baud_rate) {
    return false;
}

uint32_t tt_hal_uart_get_baud_rate(UartHandle handle) {
    return 0;
}

void tt_hal_uart_flush_input(UartHandle handle) {
}

}
