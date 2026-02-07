#include <Tactility/hal/gps/Ublox.h>
#include <Tactility/hal/gps/UbloxMessages.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/Logger.h>

#include <tactility/device.h>
#include <tactility/drivers/uart_controller.h>

#include <cstring>

namespace tt::hal::gps::ublox {

static const auto LOGGER = Logger("Ublox");

bool initUblox6(::Device* uart);
bool initUblox789(::Device* uart, GpsModel model);
bool initUblox10(::Device* uart);

#define SEND_UBX_PACKET(UART, BUFFER, TYPE, ID, DATA, ERRMSG, TIMEOUT_MILLIS) \
    do { \
        auto msglen = makePacket(TYPE, ID, DATA, sizeof(DATA), BUFFER); \
        uart_controller_write_bytes(UART, BUFFER, msglen, TIMEOUT_MILLIS / portTICK_PERIOD_MS); \
        if (getAck(UART, TYPE, ID, TIMEOUT_MILLIS) != GpsResponse::Ok) { \
            LOGGER.info("Sending packet failed: {}", #ERRMSG); \
        } \
    } while (0)

void checksum(uint8_t* message, size_t length) {
    uint8_t CK_A = 0, CK_B = 0;

    // Calculate the checksum, starting from the CLASS field (which is message[2])
    for (size_t i = 2; i < length - 2; i++) {
        CK_A = (CK_A + message[i]) & 0xFF;
        CK_B = (CK_B + CK_A) & 0xFF;
    }

    // Place the calculated checksum values in the message
    message[length - 2] = CK_A;
    message[length - 1] = CK_B;
}

uint8_t makePacket(uint8_t classId, uint8_t messageId, const uint8_t* payload, uint8_t payloadSize, uint8_t* bufferOut) {
    // Construct the UBX packet
    bufferOut[0] = 0xB5U; // header
    bufferOut[1] = 0x62U; // header
    bufferOut[2] = classId; // class
    bufferOut[3] = messageId; // id
    bufferOut[4] = payloadSize; // length
    bufferOut[5] = 0x00U;

    bufferOut[6 + payloadSize] = 0x00U; // CK_A
    bufferOut[7 + payloadSize] = 0x00U; // CK_B

    for (int i = 0; i < payloadSize; i++) {
        bufferOut[6 + i] = payload[i];
    }
    checksum(bufferOut, (payloadSize + 8U));
    return (payloadSize + 8U);
}

GpsResponse getAck(::Device* uart, uint8_t class_id, uint8_t msg_id, uint32_t waitMillis) {
    uint8_t b;
    uint8_t ack = 0;
    const uint8_t ackP[2] = {class_id, msg_id};
    uint8_t buf[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t startTime = kernel::getTicks();
    TickType_t waitTicks = pdMS_TO_TICKS(waitMillis);
    const char frame_errors[] = "More than 100 frame errors";
    int sCounter = 0;
#ifdef GPS_DEBUG
    std::string debugmsg = "";
#endif

    for (int j = 2; j < 6; j++) {
        buf[8] += buf[j];
        buf[9] += buf[8];
    }

    for (int j = 0; j < 2; j++) {
        buf[6 + j] = ackP[j];
        buf[8] += buf[6 + j];
        buf[9] += buf[8];
    }

    while (kernel::getTicks() - startTime < waitTicks) {
        if (ack > 9) {
#ifdef GPS_DEBUG
            LOGGER.info("Got ACK for class {:02X} message {:02X} in {}ms", class_id, msg_id, kernel::getMillis() - startTime);
#endif
            return GpsResponse::Ok; // ACK received
        }
        size_t available = 0;
        uart_controller_get_available(uart, &available);
        if (available > 0) {
            uart_controller_read_byte(uart, &b, 1);
            if (b == frame_errors[sCounter]) {
                sCounter++;
                if (sCounter == 26) {
#ifdef GPS_DEBUG

                    LOGGER.info("%s", debugmsg.c_str());
#endif
                    return GpsResponse::FrameErrors;
                }
            } else {
                sCounter = 0;
            }
#ifdef GPS_DEBUG
            debugmsg += std::format("%02X", b);
#endif
            if (b == buf[ack]) {
                ack++;
            } else {
                if (ack == 3 && b == 0x00) { // UBX-ACK-NAK message
#ifdef GPS_DEBUG
                    LOGGER.info("%s", debugmsg.c_str());
#endif
                    LOGGER.warn("Got NAK for class {:02X} message {:02X}", class_id, msg_id);
                    return GpsResponse::NotAck; // NAK received
                }
                ack = 0; // Reset the acknowledgement counter
            }
        }
    }
#ifdef GPS_DEBUG
    LOGGER.info("%s", debugmsg.c_str());
    LOGGER.warn("No response for class %02X message %02X", class_id, msg_id);
#endif
    return GpsResponse::None; // No response received within timeout
}

static int getAck(::Device* uart, uint8_t* buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedId, uint32_t timeoutMillis) {
    uint16_t ubxFrameCounter = 0;
    TickType_t startTime = kernel::getTicks();
    TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMillis);
    uint16_t needRead = 0;

    while ((kernel::getTicks() - startTime) < timeoutTicks) {
        size_t available = 0;
        uart_controller_get_available(uart, &available);
        while (available > 0) {
            uint8_t c;
            uart_controller_read_byte(uart, &c, 1);
            available--;

            switch (ubxFrameCounter) {
                case 0:
                    if (c == 0xB5) {
                        ubxFrameCounter++;
                    }
                    break;
                case 1:
                    if (c == 0x62) {
                        ubxFrameCounter++;
                    } else {
                        ubxFrameCounter = 0;
                    }
                    break;
                case 2:
                    if (c == requestedClass) {
                        ubxFrameCounter++;
                    } else {
                        ubxFrameCounter = 0;
                    }
                    break;
                case 3:
                    if (c == requestedId) {
                        ubxFrameCounter++;
                    } else {
                        ubxFrameCounter = 0;
                    }
                    break;
                case 4:
                    needRead = c;
                    ubxFrameCounter++;
                    break;
                case 5: {
                    // Payload length msb
                    needRead |= (c << 8);
                    ubxFrameCounter++;
                    // Check for buffer overflow
                    if (needRead >= size) {
                        ubxFrameCounter = 0;
                        break;
                    }
                    auto read_bytes = 0U;
                    uart_controller_read_bytes(uart, buffer, needRead, 250 / portTICK_PERIOD_MS);
                    if (read_bytes != needRead) {
                        ubxFrameCounter = 0;
                    } else {
                        // return payload length
#ifdef GPS_DEBUG
                        LOGGER.info("Got ACK for class {:02X} message {:02X} in {}ms", requestedClass, requestedId, kernel::getMillis() - startTime);
#endif
                        return needRead;
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
    return 0;
}

static struct uBloxGnssModelInfo {
    char swVersion[30];
    char hwVersion[10];
    uint8_t extensionNo;
    char extension[10][30];
    uint8_t protocol_version;
} ublox_info;

GpsModel probe(::Device* uart) {
    LOGGER.info("Probing for U-blox");
    constexpr auto DETECTED_MESSAGE = "{} detected, using {} Module";

    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x00, 0x00};
    checksum(cfg_rate, sizeof(cfg_rate));
    uart_controller_flush_input(uart);
    uart_controller_write_bytes(uart, cfg_rate, sizeof(cfg_rate), 500 / portTICK_PERIOD_MS);
    // Check that the returned response class and message ID are correct
    GpsResponse response = getAck(uart, 0x06, 0x08, 750);
    if (response == GpsResponse::None) {
        LOGGER.warn("No GNSS Module");
        return GpsModel::Unknown;
    } else if (response == GpsResponse::FrameErrors) {
        LOGGER.warn("UBlox Frame Errors");
    }

    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));
    uint8_t _message_MONVER[8] = {
        0xB5, 0x62, // Sync message for UBX protocol
        0x0A, 0x04, // Message class and ID (UBX-MON-VER)
        0x00, 0x00, // Length of payload (we're asking for an answer, so no payload)
        0x00, 0x00 // Checksum
    };
    //  Get Ublox gnss module hardware and software info
    checksum(_message_MONVER, sizeof(_message_MONVER));
    uart_controller_flush_input(uart);
    uart_controller_write_bytes(uart, _message_MONVER, sizeof(_message_MONVER), 500);

    uint16_t ack_response_len = getAck(uart, buffer, sizeof(buffer), 0x0A, 0x04, 1200);
    if (ack_response_len) {
        uint16_t position = 0;
        for (char& i: ublox_info.swVersion) {
            i = buffer[position];
            position++;
        }
        for (char& i: ublox_info.hwVersion) {
            i = buffer[position];
            position++;
        }

        while (ack_response_len >= position + 30) {
            for (int i = 0; i < 30; i++) {
                ublox_info.extension[ublox_info.extensionNo][i] = buffer[position];
                position++;
            }
            ublox_info.extensionNo++;
            if (ublox_info.extensionNo > 9)
                break;
        }

        LOGGER.info("Module Info:");
        LOGGER.info("Soft version: {}", ublox_info.swVersion);
        LOGGER.info("Hard version: {}", ublox_info.hwVersion);
        LOGGER.info("Extensions: {}", ublox_info.extensionNo);
        for (int i = 0; i < ublox_info.extensionNo; i++) {
            LOGGER.info("  %s", ublox_info.extension[i]);
        }

        memset(buffer, 0, sizeof(buffer));

        // tips: extensionNo field is 0 on some 6M GNSS modules
        for (int i = 0; i < ublox_info.extensionNo; ++i) {
            if (!strncmp(ublox_info.extension[i], "MOD=", 4)) {
                strncpy((char*)buffer, &(ublox_info.extension[i][4]), sizeof(buffer));
            } else if (!strncmp(ublox_info.extension[i], "PROTVER", 7)) {
                char* ptr = nullptr;
                memset(buffer, 0, sizeof(buffer));
                strncpy((char*)buffer, &(ublox_info.extension[i][8]), sizeof(buffer));
                LOGGER.info("Protocol Version: {}", (char*)buffer);
                if (strlen((char*)buffer)) {
                    ublox_info.protocol_version = strtoul((char*)buffer, &ptr, 10);
                    LOGGER.info("ProtVer={}", ublox_info.protocol_version);
                } else {
                    ublox_info.protocol_version = 0;
                }
            }
        }
        if (strncmp(ublox_info.hwVersion, "00040007", 8) == 0) {
            LOGGER.info(DETECTED_MESSAGE, "U-blox 6", "6");
            return GpsModel::UBLOX6;
        } else if (strncmp(ublox_info.hwVersion, "00070000", 8) == 0) {
            LOGGER.info(DETECTED_MESSAGE, "U-blox 7", "7");
            return GpsModel::UBLOX7;
        } else if (strncmp(ublox_info.hwVersion, "00080000", 8) == 0) {
            LOGGER.info(DETECTED_MESSAGE, "U-blox 8", "8");
            return GpsModel::UBLOX8;
        } else if (strncmp(ublox_info.hwVersion, "00190000", 8) == 0) {
            LOGGER.info(DETECTED_MESSAGE, "U-blox 9", "9");
            return GpsModel::UBLOX9;
        } else if (strncmp(ublox_info.hwVersion, "000A0000", 8) == 0) {
            LOGGER.info(DETECTED_MESSAGE, "U-blox 10", "10");
            return GpsModel::UBLOX10;
        }
    }

    return GpsModel::Unknown;
}

bool init(::Device* uart, GpsModel model) {
    LOGGER.info("U-blox init");
    switch (model) {
        case GpsModel::UBLOX6:
            return initUblox6(uart);
        case GpsModel::UBLOX7:
        case GpsModel::UBLOX8:
        case GpsModel::UBLOX9:
            return initUblox789(uart, model);
        case GpsModel::UBLOX10:
            return initUblox10(uart);
        default:
            LOGGER.error("Unknown or unsupported U-blox model");
            return false;
    }
}

bool initUblox10(::Device* uart) {
    uint8_t buffer[256];
    kernel::delayMillis(1000);
    uart_controller_flush_input(uart);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_DISABLE_NMEA_RAM, "disable NMEA messages in M10 RAM", 300);
    kernel::delayMillis(750);
    uart_controller_flush_input(uart);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_DISABLE_NMEA_BBR, "disable NMEA messages in M10 BBR", 300);
    kernel::delayMillis(750);
    uart_controller_flush_input(uart);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_DISABLE_TXT_INFO_RAM, "disable Info messages for M10 GPS RAM", 300);
    kernel::delayMillis(750);
    uart_controller_flush_input(uart);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_DISABLE_TXT_INFO_BBR, "disable Info messages for M10 GPS BBR", 300);
    kernel::delayMillis(750);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_PM_RAM, "enable powersave for M10 GPS RAM", 300);
    kernel::delayMillis(750);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_PM_BBR, "enable powersave for M10 GPS BBR", 300);
    kernel::delayMillis(750);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_ITFM_RAM, "enable jam detection M10 GPS RAM", 300);
    kernel::delayMillis(750);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_ITFM_BBR, "enable jam detection M10 GPS BBR", 300);
    kernel::delayMillis(750);
    // Here is where the init commands should go to do further M10 initialization.
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_DISABLE_SBAS_RAM, "disable SBAS M10 GPS RAM", 300);
    kernel::delayMillis(750); // will cause a receiver restart so wait a bit
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_DISABLE_SBAS_BBR, "disable SBAS M10 GPS BBR", 300);
    kernel::delayMillis(750); // will cause a receiver restart so wait a bit

    // Done with initialization

    // Enable wanted NMEA messages in BBR layer so they will survive a periodic sleep
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_ENABLE_NMEA_BBR, "enable messages for M10 GPS BBR", 300);
    kernel::delayMillis(750);
    // Enable wanted NMEA messages in RAM layer
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x8A, _message_VALSET_ENABLE_NMEA_RAM, "enable messages for M10 GPS RAM", 500);
    kernel::delayMillis(750);

    // As the M10 has no flash, the best we can do to preserve the config is to set it in RAM and BBR.
    // BBR will survive a restart, and power off for a while, but modules with small backup
    // batteries or super caps will not retain the config for a long power off time.
    auto packet_size = makePacket(0x06, 0x09, _message_SAVE_10, sizeof(_message_SAVE_10), buffer);
    uart_controller_write_bytes(uart, buffer, packet_size, 2000 / portTICK_PERIOD_MS);
    if (getAck(uart, 0x06, 0x09, 2000) != GpsResponse::Ok) {
        LOGGER.warn("Unable to save GNSS module config");
    } else {
        LOGGER.info("GNSS module configuration saved!");
    }
    return true;
}

bool initUblox789(::Device* uart, GpsModel model) {
    uint8_t buffer[256];
    if (model == GpsModel::UBLOX7) {
        LOGGER.debug("Set GPS+SBAS");
        auto msglen = makePacket(0x06, 0x3e, _message_GNSS_7, sizeof(_message_GNSS_7), buffer);
        uart_controller_write_bytes(uart, buffer, msglen, 800 / portTICK_PERIOD_MS);
    } else { // 8,9
        auto msglen = makePacket(0x06, 0x3e, _message_GNSS_8, sizeof(_message_GNSS_8), buffer);
        uart_controller_write_bytes(uart, buffer, msglen, 800 / portTICK_PERIOD_MS);
    }

    if (getAck(uart, 0x06, 0x3e, 800) == GpsResponse::NotAck) {
        // It's not critical if the module doesn't acknowledge this configuration.
        LOGGER.debug("reconfigure GNSS - defaults maintained. Is this module GPS-only?");
    } else {
        if (model == GpsModel::UBLOX7) {
            LOGGER.info("GPS+SBAS configured");
        } else { // 8,9
            LOGGER.info("GPS+SBAS+GLONASS+Galileo configured");
        }
        // Documentation say, we need wait at least 0.5s after reconfiguration of GNSS module, before sending next
        // commands for the M8 it tends to be more. 1 sec should be enough
        kernel::delayMillis(1000);
    }

    uart_controller_flush_input(uart);

    SEND_UBX_PACKET(uart, buffer, 0x06, 0x02, _message_DISABLE_TXT_INFO, "disable text info messages", 500);

    if (model == GpsModel::UBLOX8) { // 8
        uart_controller_flush_input(uart);
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x39, _message_JAM_8, "enable interference resistance", 500);

        uart_controller_flush_input(uart);
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x23, _message_NAVX5_8, "configure NAVX5_8 settings", 500);
    } else { // 6,7,9
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x39, _message_JAM_6_7, "enable interference resistance", 500);
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x23, _message_NAVX5, "configure NAVX5 settings", 500);
    }

    // Turn off unwanted NMEA messages, set update rate
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x08, _message_1HZ, "set GPS update rate", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GLL, "disable NMEA GLL", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GSA, "enable NMEA GSA", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GSV, "disable NMEA GSV", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_VTG, "disable NMEA VTG", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_RMC, "enable NMEA RMC", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GGA, "enable NMEA GGA", 500);

    if (ublox_info.protocol_version >= 18) {
        uart_controller_flush_input(uart);
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x86, _message_PMS, "enable powersave for GPS", 500);
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x3B, _message_CFG_PM2, "enable powersave details for GPS", 500);

        // For M8 we want to enable NMEA version 4.10 so we can see the additional satellites.
        if (model == GpsModel::UBLOX8) {
            uart_controller_flush_input(uart);
            SEND_UBX_PACKET(uart, buffer, 0x06, 0x17, _message_NMEA, "enable NMEA 4.10", 500);
        }
    } else {
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x11, _message_CFG_RXM_PSM, "enable powersave mode for GPS", 500);
        SEND_UBX_PACKET(uart, buffer, 0x06, 0x3B, _message_CFG_PM2, "enable powersave details for GPS", 500);
    }

    auto packet_size = makePacket(0x06, 0x09, _message_SAVE, sizeof(_message_SAVE), buffer);
    uart_controller_write_bytes(uart, buffer, packet_size, 2000 / portTICK_PERIOD_MS);
    if (getAck(uart, 0x06, 0x09, 2000) != GpsResponse::Ok) {
        LOGGER.warn("Unable to save GNSS module config");
    } else {
        LOGGER.info("GNSS module configuration saved!");
    }
    return true;
}

bool initUblox6(::Device* uart) {
    uint8_t buffer[256];

    uart_controller_flush_input(uart);

    SEND_UBX_PACKET(uart, buffer, 0x06, 0x02, _message_DISABLE_TXT_INFO, "disable text info messages", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x39, _message_JAM_6_7, "enable interference resistance", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x23, _message_NAVX5, "configure NAVX5 settings", 500);

    // Turn off unwanted NMEA messages, set update rate
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x08, _message_1HZ, "set GPS update rate", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GLL, "disable NMEA GLL", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GSA, "enable NMEA GSA", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GSV, "disable NMEA GSV", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_VTG, "disable NMEA VTG", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_RMC, "enable NMEA RMC", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_GGA, "enable NMEA GGA", 500);

    uart_controller_flush_input(uart);

    SEND_UBX_PACKET(uart, buffer, 0x06, 0x11, _message_CFG_RXM_ECO, "enable powersave ECO mode for Neo-6", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x3B, _message_CFG_PM2, "enable powersave details for GPS", 500);
    SEND_UBX_PACKET(uart, buffer, 0x06, 0x01, _message_AID, "disable UBX-AID", 500);

    auto packet_size = makePacket(0x06, 0x09, _message_SAVE, sizeof(_message_SAVE), buffer);
    uart_controller_write_bytes(uart, buffer, packet_size, 2000);
    if (getAck(uart, 0x06, 0x09, 2000) != GpsResponse::Ok) {
        LOGGER.warn("Unable to save GNSS module config");
    } else {
        LOGGER.info("GNSS module config saved!");
    }
    return true;
}

} // namespace tt::hal::gps::ublox
