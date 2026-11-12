// SPDX-License-Identifier: GPL-3.0
#include <gps_generic/private/cas_messages.h>
#include <gps_generic/private/init.h>
#include <gps_generic/private/interrupt.h>
#include <gps_generic/private/ublox.h>
#include <gps_generic/private/gps_response.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <cstring>

constexpr auto* TAG = "gps-meshtastic";

bool init_mtk(Device* uart, const std::atomic<bool>& interrupted);
bool init_mtk_l76b(Device* uart, const std::atomic<bool>& interrupted);
bool init_mtk_pa1616s(Device* uart, const std::atomic<bool>& interrupted);
bool init_atgm336h(Device* uart, const std::atomic<bool>& interrupted);
bool init_uc6580(Device* uart, const std::atomic<bool>& interrupted);
bool init_ag33xx(Device* uart, const std::atomic<bool>& interrupted);

// region CAS

// Calculate the checksum for a CAS packet
static void cas_checksum(uint8_t* message, size_t length) {
    uint32_t cksum = ((uint32_t)message[5] << 24); // Message ID
    cksum += ((uint32_t)message[4]) << 16;         // Class
    cksum += message[2];                           // Payload Len

    // Iterate over the payload as a series of uint32_t's and
    // accumulate the cksum
    for (size_t i = 0; i < (length - 10) / 4; i++) {
        uint32_t pl = 0;
        memcpy(&pl, (message + 6) + (i * sizeof(uint32_t)), sizeof(uint32_t)); // avoid pointer dereference
        cksum += pl;
    }

    // Place the checksum values in the message
    message[length - 4] = (cksum & 0xFF);
    message[length - 3] = (cksum & (0xFF << 8)) >> 8;
    message[length - 2] = (cksum & (0xFF << 16)) >> 16;
    message[length - 1] = (cksum & (0xFF << 24)) >> 24;
}

// Function to create a CAS packet for editing in memory
static uint8_t make_cas_packet(uint8_t* buffer, uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t* msg) {
    // General CAS structure
    //        | H1   | H2   | payload_len | cls  | msg  | Payload       ...   | Checksum                  |
    // Size:  | 1    | 1    | 2           | 1    | 1    | payload_len         | 4                         |
    // Pos:   | 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7      ...   | 6 + payload_len ...       |
    //        |------|------|-------------|------|------|------|--------------|---------------------------|
    //        | 0xBA | 0xCE | 0xXX | 0xXX | 0xXX | 0xXX | 0xXX | 0xXX   ...   | 0xXX | 0xXX | 0xXX | 0xXX |

    // Construct the CAS packet
    buffer[0] = 0xBA;         // header 1 (0xBA)
    buffer[1] = 0xCE;         // header 2 (0xCE)
    buffer[2] = payload_size; // length 1
    buffer[3] = 0;            // length 2
    buffer[4] = class_id;     // class
    buffer[5] = msg_id;       // id

    buffer[6 + payload_size] = 0x00; // Checksum
    buffer[7 + payload_size] = 0x00;
    buffer[8 + payload_size] = 0x00;
    buffer[9 + payload_size] = 0x00;

    for (int i = 0; i < payload_size; i++) {
        buffer[6 + i] = msg[i];
    }
    cas_checksum(buffer, (payload_size + 10));

    return (payload_size + 10);
}

static GpsResponse get_ack_cas(Device* uart, const std::atomic<bool>& interrupted, uint8_t class_id, uint8_t msg_id, uint32_t wait_millis) {
    uint32_t start_time = get_millis();
    uint8_t buffer[CAS_MESSAGE_ACK_NACK_SIZE] = {0};
    uint8_t buffer_pos = 0;
    TickType_t wait_ticks = pdMS_TO_TICKS(wait_millis);

    // CAS-ACK-(N)ACK structure
    //         | H1   | H2   | Payload Len | cls  | msg  | Payload                   | Checksum (4)              |
    //         |      |      |             |      |      | Cls  | Msg  | Reserved    |                           |
    //         |------|------|-------------|------|------|------|------|-------------|---------------------------|
    // ACK-NACK| 0xBA | 0xCE | 0x04 | 0x00 | 0x05 | 0x00 | 0xXX | 0xXX | 0x00 | 0x00 | 0xXX | 0xXX | 0xXX | 0xXX |
    // ACK-ACK | 0xBA | 0xCE | 0x04 | 0x00 | 0x05 | 0x01 | 0xXX | 0xXX | 0x00 | 0x00 | 0xXX | 0xXX | 0xXX | 0xXX |

    while (get_ticks() - start_time < wait_ticks && !interrupted) {
        size_t available = 0;
        uart_controller_get_available(uart, &available);
        if (available > 0) {
            uart_controller_read_byte(uart, &buffer[buffer_pos++], 1);

            // keep looking at the first two bytes of buffer until
            // we have found the CAS frame header (0xBA, 0xCE), if not
            // keep reading bytes until we find a frame header or we run
            // out of time.
            if ((buffer_pos == 2) && !(buffer[0] == 0xBA && buffer[1] == 0xCE)) {
                buffer[0] = buffer[1];
                buffer[1] = 0;
                buffer_pos = 1;
            }
        }

        // we have read all the bytes required for the Ack/Nack (14-bytes)
        // and we must have found a frame to get this far
        if (buffer_pos == sizeof(buffer) - 1) {
            uint8_t msg_cls = buffer[4];     // message class should be 0x05
            uint8_t msg_msg_id = buffer[5];  // message id should be 0x00 or 0x01
            uint8_t payload_cls = buffer[6]; // payload class id
            uint8_t payload_msg = buffer[7]; // payload message id

            // Check for an ACK-ACK for the specified class and message id
            if ((msg_cls == 0x05) && (msg_msg_id == 0x01) && payload_cls == class_id && payload_msg == msg_id) {
                return GpsResponse::Ok;
            }

            // Check for an ACK-NACK for the specified class and message id
            if ((msg_cls == 0x05) && (msg_msg_id == 0x00) && payload_cls == class_id && payload_msg == msg_id) {
                return GpsResponse::NotAck;
            }

            // This isn't the frame we are looking for, clear the buffer
            // and try again until we run out of time.
            memset(buffer, 0x0, sizeof(buffer));
            buffer_pos = 0;
        }
    }
    return GpsResponse::None;
}

// endregion

bool gps_init(Device* uart, GpsModel type, const std::atomic<bool>& interrupted) {
    switch (type) {
        case GPS_MODEL_UNKNOWN:
            check(false);
        case GPS_MODEL_AG3335:
        case GPS_MODEL_AG3352:
            return init_ag33xx(uart, interrupted);
        case GPS_MODEL_ATGM336H:
            return init_atgm336h(uart, interrupted);
        case GPS_MODEL_LS20031:
            return true;
        case GPS_MODEL_MTK:
            return init_mtk(uart, interrupted);
        case GPS_MODEL_MTK_L76B:
            return init_mtk_l76b(uart, interrupted);
        case GPS_MODEL_MTK_PA1616S:
            return init_mtk_pa1616s(uart, interrupted);
        case GPS_MODEL_UBLOX6:
        case GPS_MODEL_UBLOX7:
        case GPS_MODEL_UBLOX8:
        case GPS_MODEL_UBLOX9:
        case GPS_MODEL_UBLOX10:
            return gps_ublox::init(uart, type, interrupted);
        case GPS_MODEL_UC6580:
            return init_uc6580(uart, interrupted);
    }

    LOG_I(TAG, "Init not implemented %d", static_cast<int>(type));
    return false;
}

bool init_ag33xx(Device* uart, const std::atomic<bool>& interrupted) {
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR066,1,0,1,0,0,1*3B\r\n", 25, 250); // Enable GPS+GALILEO+NAVIC

    // Configure NMEA (sentences will output once per fix)
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,0,1*3F\r\n", 17, 250); // GGA ON
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,1,0*3F\r\n", 17, 250); // GLL OFF
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,2,1*3D\r\n", 17, 250); // GSA ON
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,3,1*3C\r\n", 17, 250); // GSV ON
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,4,1*3B\r\n", 17, 250); // RMC ON
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,5,0*3B\r\n", 17, 250); // VTG OFF
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR062,6,0*38\r\n", 17, 250); // ZDA ON

    if (!gps_delay_millis(interrupted, 250)) {

        return false;

    }
    uart_controller_write_bytes(uart, (const uint8_t*)"$PAIR513*3D\r\n", 13, 250); // save configuration
    return true;
}

bool init_uc6580(Device* uart, const std::atomic<bool>& interrupted) {
    // The Unicore UC6580 can use a lot of sat systems, enable it to
    // use GPS L1 & L5 + BDS B1I & B2a + GLONASS L1 + GALILEO E1 & E5a + SBAS + QZSS
    // This will reset the receiver, so wait a bit afterwards
    // The paranoid will wait for the OK*04 confirmation response after each command.
    uart_controller_write_bytes(uart, (const uint8_t*)"$CFGSYS,h35155\r\n", 16, 250);
    if (!gps_delay_millis(interrupted, 750)) {
        return false;
    }
    // Must be done after the CFGSYS command
    // GSV lists the satellites in view for apps
    uart_controller_write_bytes(uart, (const uint8_t*)"$CFGMSG,0,3,1\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // GSA lists the satellites used in the fix and the DOP values
    uart_controller_write_bytes(uart, (const uint8_t*)"$CFGMSG,0,2,1\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Turn off NOTICE __TXT messages, these may provide Unicore some info but we don't care.
    uart_controller_write_bytes(uart, (const uint8_t*)"$CFGMSG,6,0,0\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    uart_controller_write_bytes(uart, (const uint8_t*)"$CFGMSG,6,1,0\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    return true;
}

bool init_atgm336h(Device* uart, const std::atomic<bool>& interrupted) {
    uint8_t buffer[256];

    // Set the intial configuration of the device - these _should_ work for most AT6558 devices
    int msglen = make_cas_packet(buffer, 0x06, 0x07, sizeof(CAS_MESSAGE_CFG_NAVX_CONF), CAS_MESSAGE_CFG_NAVX_CONF);
    uart_controller_write_bytes(uart, buffer, msglen, 250);
    if (get_ack_cas(uart, interrupted, 0x06, 0x07, 250) != GpsResponse::Ok) {
        if (interrupted) {
            return false;
        }
        LOG_W(TAG, "ATGM336H: Could not set Config");
    }

    // Set the update frequence to 1Hz
    msglen = make_cas_packet(buffer, 0x06, 0x04, sizeof(CAS_MESSAGE_CFG_RATE_1HZ), CAS_MESSAGE_CFG_RATE_1HZ);
    uart_controller_write_bytes(uart, buffer, msglen, 250);
    if (get_ack_cas(uart, interrupted, 0x06, 0x04, 250) != GpsResponse::Ok) {
        if (interrupted) {
            return false;
        }
        LOG_W(TAG, "ATGM336H: Could not set Update Frequency");
    }

    // Set the NEMA output messages
    // RMC and GGA for the position, GSA and GSV for the satellites
    uint8_t fields[] = {CAS_NEMA_RMC, CAS_NEMA_GGA, CAS_NEMA_GSA, CAS_NEMA_GSV};
    for (unsigned int i = 0; i < sizeof(fields); i++) {
        // Construct a CAS-CFG-MSG packet
        uint8_t cas_cfg_msg_packet[] = {0x4e, fields[i], 0x01, 0x00};
        msglen = make_cas_packet(buffer, 0x06, 0x01, sizeof(cas_cfg_msg_packet), cas_cfg_msg_packet);
        uart_controller_write_bytes(uart, buffer, msglen, 250);
        if (get_ack_cas(uart, interrupted, 0x06, 0x01, 250) != GpsResponse::Ok) {
            if (interrupted) {
                return false;
            }
            LOG_W(TAG, "ATGM336H: Could not enable NMEA MSG: %u", fields[i]);
        }
    }
    return true;
}

bool init_mtk_pa1616s(Device* uart, const std::atomic<bool>& interrupted) {
    // PA1616S is used in some GPS breakout boards from Adafruit
    // PA1616S does not have GLONASS capability. PA1616D does, but is not implemented here.
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK353,1,0,0,0,0*2A\r\n", 23, 250);
    // Above command will reset the GPS and takes longer before it will accept new commands
    if (!gps_delay_millis(interrupted, 1000)) {
        return false;
    }
    // Ask for RMC, GGA, GSA and GSV
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK314,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n", 51, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Enable SBAS / WAAS
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK301,2*2E\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    return true;
}

bool init_mtk_l76b(Device* uart, const std::atomic<bool>& interrupted) {
    // Waveshare Pico-GPS hat uses the L76B with 9600 baud
    // Initialize the L76B Chip, use GPS + GLONASS
    // See note in L76_Series_GNSS_Protocol_Specification, chapter 3.29
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK353,1,1,0,0,0*2B\r\n", 23, 250);
    // Above command will reset the GPS and takes longer before it will accept new commands
    if (!gps_delay_millis(interrupted, 1000)) {
        return false;
    }
    // Ask for RMC, GGA, GSA and GSV
    // See note in L76_Series_GNSS_Protocol_Specification, chapter 2.1
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK314,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n", 51, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Enable SBAS
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK301,2*2E\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Enable PPS for 2D/3D fix only
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK285,3,100*3F\r\n", 19, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Switch to Fitness Mode, for running and walking purpose with low speed (<5 m/s)
    uart_controller_write_bytes(uart, (const uint8_t*)"$PMTK886,1*29\r\n", 15, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    return true;
}

bool init_mtk(Device* uart, const std::atomic<bool>& interrupted) {
    // Initialize the L76K Chip, use GPS + GLONASS + BEIDOU
    uart_controller_write_bytes(uart, (const uint8_t*)"$PCAS04,7*1E\r\n", 14, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Ask for GGA, GSA, GSV and RMC
    uart_controller_write_bytes(uart, (const uint8_t*)"$PCAS03,1,0,1,1,1,0,0,0,0,0,,,0,0*02\r\n", 38, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
    uart_controller_write_bytes(uart, (const uint8_t*)"$PCAS11,3*1E\r\n", 14, 250);
    if (!gps_delay_millis(interrupted, 250)) {
        return false;
    }
    return true;
}
