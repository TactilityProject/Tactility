// SPDX-License-Identifier: Apache-2.0
#include "probe.h"
#include "gps_response.h"
#include "ublox.h"

#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <cstring>

constexpr auto* TAG = "Gps";

/**
 * From: https://github.com/meshtastic/firmware/blob/3b0232de1b6282eacfbff6e50b68fca7e67b8511/src/meshUtils.cpp#L40
 */
static char* probe_strnstr(const char* s, const char* find, size_t slen) {
    char c;
    if ((c = *find++) != '\0') {
        char sc;
        size_t len;

        len = strlen(find);
        do {
            do {
                if (slen-- < 1 || (sc = *s++) == '\0')
                    return (nullptr);
            } while (sc != c);
            if (len > slen)
                return (nullptr);
        } while (strncmp(s, find, len) != 0);
        s--;
    }
    return ((char*)s);
}

/**
 * From: https://github.com/meshtastic/firmware/blob/f81d3b045dd1b7e3ca7870af3da915ff4399ea98/src/gps/GPS.cpp
 */
static GpsResponse get_ack(Device* uart, const char* message, uint32_t wait_millis) {
    uint8_t buffer[768] = {0};
    uint8_t b;
    int bytes_read = 0;
    uint32_t start_timeout = get_millis() + wait_millis;
    while (get_millis() < start_timeout) {
        size_t available = 0;
        uart_controller_get_available(uart, &available);
        if (available > 0) {
            uart_controller_read_byte(uart, &b, 1);

            buffer[bytes_read] = b;
            bytes_read++;
            if ((bytes_read == 767) || (b == '\r')) {
                if (probe_strnstr((char*)buffer, message, bytes_read) != nullptr) {
                    return GpsResponse::Ok;
                } else {
                    bytes_read = 0;
                }
            }
        }
    }
    return GpsResponse::None;
}

/**
 * From: https://github.com/meshtastic/firmware/blob/f81d3b045dd1b7e3ca7870af3da915ff4399ea98/src/gps/GPS.cpp
 */
#define PROBE_SIMPLE(UART, CHIP, TOWRITE, RESPONSE, DRIVER, TIMEOUT, ...)   \
    do {                                                                    \
        LOG_I(TAG, "Probing for %s (%s)", CHIP, TOWRITE);                   \
        uart_controller_flush_input(UART);                                  \
        uart_controller_write_bytes(UART, (const uint8_t*)(TOWRITE "\r\n"), strlen(TOWRITE "\r\n"), TIMEOUT); \
        if (get_ack(UART, RESPONSE, TIMEOUT) == GpsResponse::Ok) {           \
            LOG_I(TAG, "Probe detected %s %s", CHIP, #DRIVER);              \
            return DRIVER;                                                  \
        }                                                                   \
    } while (0)

/**
 * From: https://github.com/meshtastic/firmware/blob/f81d3b045dd1b7e3ca7870af3da915ff4399ea98/src/gps/GPS.cpp
 */
GpsModel gps_probe(Device* uart) {
    // Close all NMEA sentences, valid for L76K, ATGM336H (and likely other AT6558 devices)
    uart_controller_write_bytes(uart, (const uint8_t*)"$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n", 40, 500);
    delay_millis(20);

    // Close NMEA sequences on Ublox
    uart_controller_write_bytes(uart, (const uint8_t*)"$PUBX,40,GLL,0,0,0,0,0,0*5C\r\n", 29, 500);
    uart_controller_write_bytes(uart, (const uint8_t*)"$PUBX,40,GSV,0,0,0,0,0,0*59\r\n", 29, 500);
    uart_controller_write_bytes(uart, (const uint8_t*)"$PUBX,40,VTG,0,0,0,0,0,0*5E\r\n", 29, 500);
    delay_millis(20);

    // Unicore UFirebirdII Series: UC6580, UM620, UM621, UM670A, UM680A, or UM681A
    PROBE_SIMPLE(uart, "UC6580", "$PDTINFO", "UC6580", GpsModel::GPS_MODEL_UC6580, 500);
    PROBE_SIMPLE(uart, "UM600", "$PDTINFO", "UM600", GpsModel::GPS_MODEL_UC6580, 500);
    PROBE_SIMPLE(uart, "ATGM336H", "$PCAS06,1*1A", "$GPTXT,01,01,02,HW=ATGM336H", GpsModel::GPS_MODEL_ATGM336H, 500);

    /* ATGM332D series (-11(GPS), -21(BDS), -31(GPS+BDS), -51(GPS+GLONASS), -71-0(GPS+BDS+GLONASS))
    based on AT6558 */
    PROBE_SIMPLE(uart, "ATGM332D", "$PCAS06,1*1A", "$GPTXT,01,01,02,HW=ATGM332D", GpsModel::GPS_MODEL_ATGM336H, 500);

    /* Airoha (Mediatek) AG3335A/M/S, A3352Q, Quectel L89 2.0, SimCom SIM65M */
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,2,0*3C\r\n", 17, 500); // GSA OFF to reduce volume
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,3,0*3D\r\n", 17, 500); // GSV OFF to reduce volume
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR513*3D\r\n", 13, 500); // save configuration
    PROBE_SIMPLE(uart, "AG3335", "$PAIR021*39", "$PAIR021,AG3335", GpsModel::GPS_MODEL_AG3335, 500);
    PROBE_SIMPLE(uart, "AG3352", "$PAIR021*39", "$PAIR021,AG3352", GpsModel::GPS_MODEL_AG3352, 500);
    PROBE_SIMPLE(uart, "LC86", "$PQTMVERNO*58", "$PQTMVERNO,LC86", GpsModel::GPS_MODEL_AG3352, 500);

    PROBE_SIMPLE(uart, "L76K", "$PCAS06,0*1B", "$GPTXT,01,01,02,SW=", GpsModel::GPS_MODEL_MTK, 500);

    // Close all NMEA sentences, valid for L76B MTK platform (Waveshare Pico GPS)
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK514,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*2E\r\n", 51, 500);
    delay_millis(20);

    PROBE_SIMPLE(uart, "L76B", "$PMTK605*31", "Quectel-L76B", GpsModel::GPS_MODEL_MTK_L76B, 500);
    PROBE_SIMPLE(uart, "PA1616S", "$PMTK605*31", "1616S", GpsModel::GPS_MODEL_MTK_PA1616S, 500);

    auto ublox_result = gps_ublox::probe(uart);
    if (ublox_result != GpsModel::GPS_MODEL_UNKNOWN) {
        return ublox_result;
    } else {
        LOG_W(TAG, "No GNSS Module");
        return GpsModel::GPS_MODEL_UNKNOWN;
    }
}
