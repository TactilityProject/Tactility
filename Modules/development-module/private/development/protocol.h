// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <development/server.h>

#include <tactility/error.h>

#define DEVELOPMENT_PROTOCOL_VERSION 1

/** Maximum length of the hello and status lines, including the newline */
#define DEVELOPMENT_PROTOCOL_LINE_MAX 1024

#define DEVELOPMENT_FRAME_HEADER_SIZE 3
#define DEVELOPMENT_FRAME_PAYLOAD_MAX 1024

#define DEVELOPMENT_MODE_LENGTH 15
#define DEVELOPMENT_CLIENT_NAME_LENGTH DEVELOPMENT_SERVER_CLIENT_NAME_LENGTH
#define DEVELOPMENT_COMMAND_LENGTH 511

enum DevelopmentFrameType : uint8_t {
    DEVELOPMENT_FRAME_DATA = 0x01,
    // Shell mode
    DEVELOPMENT_FRAME_RESIZE = 0x02,
    DEVELOPMENT_FRAME_EXIT = 0x03,
    // Sync and app mode
    DEVELOPMENT_FRAME_DONE = 0x14,
    DEVELOPMENT_FRAME_OK = 0x15,
    DEVELOPMENT_FRAME_FAIL = 0x16,
    // Sync mode
    DEVELOPMENT_FRAME_SYNC_STAT = 0x10,
    DEVELOPMENT_FRAME_SYNC_STAT_RESULT = 0x11,
    DEVELOPMENT_FRAME_SYNC_PUSH = 0x12,
    DEVELOPMENT_FRAME_SYNC_PULL = 0x13,
    // App mode
    DEVELOPMENT_FRAME_APP_INSTALL = 0x20,
    DEVELOPMENT_FRAME_APP_UNINSTALL = 0x21,
    DEVELOPMENT_FRAME_APP_RUN = 0x22,
};

enum DevelopmentSyncFileType : uint8_t {
    DEVELOPMENT_SYNC_FILE_MISSING = 0,
    DEVELOPMENT_SYNC_FILE_REGULAR = 1,
    DEVELOPMENT_SYNC_FILE_DIRECTORY = 2,
};

struct DevelopmentHello {
    int protocol;
    char mode[DEVELOPMENT_MODE_LENGTH + 1];
    char client[DEVELOPMENT_CLIENT_NAME_LENGTH + 1];
    uint16_t columns;
    uint16_t rows;
    /** Shell mode only: runs this command instead of an interactive shell when not empty */
    char command[DEVELOPMENT_COMMAND_LENGTH + 1];
};

enum class DevelopmentHelloError {
    None,
    Malformed,
    UnsupportedProtocol,
    UnsupportedMode,
};

/**
 * Parses and validates the JSON hello line that a client sends after the TLS handshake.
 * @param[in] json the line, without its newline
 * @param[out] out_message the reason, for anything but DevelopmentHelloError::None
 */
DevelopmentHelloError development_protocol_parse_hello(const char* json, size_t length, DevelopmentHello* out_hello, const char** out_message);

/**
 * Writes the JSON status line that answers a hello, including its newline.
 * @param[in] status "accepted", "rejected" or "error"
 * @param[in] message only written when not NULL
 * @return the line length, or 0 when @a out_size is too small
 */
size_t development_protocol_format_status(const char* status, const char* message, char* out_line, size_t out_size);

/**
 * Writes the JSON line that answers an "info" hello, including its newline.
 * @return the line length, or 0 when @a out_size is too small
 */
size_t development_protocol_format_info(const char* vendor, const char* name, const char* id, const char* version, char* out_line, size_t out_size);

struct DevelopmentFrame {
    DevelopmentFrameType type;
    const uint8_t* payload;
    uint16_t length;
};

enum class DevelopmentFrameParseResult {
    Ok,
    Incomplete,
    Invalid,
};

/**
 * Parses the first frame in @a data.
 * @param[out] out_frame payload points into @a data
 * @param[out] out_consumed the size of the parsed frame
 * @return DevelopmentFrameParseResult::Invalid when the frame is longer than DEVELOPMENT_FRAME_PAYLOAD_MAX
 */
DevelopmentFrameParseResult development_frame_parse(const uint8_t* data, size_t size, DevelopmentFrame* out_frame, size_t* out_consumed);

/** Writes a frame header for a payload of @a length bytes */
void development_frame_write_header(DevelopmentFrameType type, uint16_t length, uint8_t out_header[DEVELOPMENT_FRAME_HEADER_SIZE]);
