// SPDX-License-Identifier: Apache-2.0
#include <development/protocol.h>

#include <cJSON.h>

#include <cstring>

namespace {

constexpr uint16_t DEFAULT_COLUMNS = 80;
constexpr uint16_t DEFAULT_ROWS = 24;

bool copyString(const cJSON* item, char* out, size_t outSize) {
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    const size_t length = strlen(item->valuestring);
    if (length >= outSize) {
        return false;
    }
    memcpy(out, item->valuestring, length + 1);
    return true;
}

uint16_t readDimension(const cJSON* item, uint16_t fallback) {
    if (!cJSON_IsNumber(item) || item->valuedouble < 1 || item->valuedouble > UINT16_MAX) {
        return fallback;
    }
    return static_cast<uint16_t>(item->valuedouble);
}

} // namespace

DevelopmentHelloError development_protocol_parse_hello(const char* json, size_t length, DevelopmentHello* out_hello, const char** out_message) {
    cJSON* root = cJSON_ParseWithLength(json, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        *out_message = "malformed hello";
        return DevelopmentHelloError::Malformed;
    }

    DevelopmentHello hello {};
    const cJSON* protocol = cJSON_GetObjectItemCaseSensitive(root, "protocol");
    if (!cJSON_IsNumber(protocol) ||
        !copyString(cJSON_GetObjectItemCaseSensitive(root, "mode"), hello.mode, sizeof(hello.mode)) ||
        !copyString(cJSON_GetObjectItemCaseSensitive(root, "client"), hello.client, sizeof(hello.client))) {
        cJSON_Delete(root);
        *out_message = "malformed hello";
        return DevelopmentHelloError::Malformed;
    }
    hello.protocol = protocol->valueint;
    hello.columns = readDimension(cJSON_GetObjectItemCaseSensitive(root, "columns"), DEFAULT_COLUMNS);
    hello.rows = readDimension(cJSON_GetObjectItemCaseSensitive(root, "rows"), DEFAULT_ROWS);
    const cJSON* command = cJSON_GetObjectItemCaseSensitive(root, "command");
    const bool hasCommand = command != nullptr;
    if (hasCommand && !copyString(command, hello.command, sizeof(hello.command))) {
        cJSON_Delete(root);
        *out_message = "malformed hello";
        return DevelopmentHelloError::Malformed;
    }
    cJSON_Delete(root);

    if (hello.protocol != DEVELOPMENT_PROTOCOL_VERSION) {
        *out_message = "unsupported protocol version";
        return DevelopmentHelloError::UnsupportedProtocol;
    }
    if (strcmp(hello.mode, "shell") != 0 && strcmp(hello.mode, "sync") != 0 && strcmp(hello.mode, "app") != 0 && strcmp(hello.mode, "info") != 0) {
        *out_message = "unsupported mode";
        return DevelopmentHelloError::UnsupportedMode;
    }
    if (hasCommand && strcmp(hello.mode, "shell") != 0) {
        *out_message = "malformed hello";
        return DevelopmentHelloError::Malformed;
    }

    *out_hello = hello;
    *out_message = nullptr;
    return DevelopmentHelloError::None;
}

size_t development_protocol_format_status(const char* status, const char* message, char* out_line, size_t out_size) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return 0;
    }
    cJSON_AddStringToObject(root, "status", status);
    if (message != nullptr) {
        cJSON_AddStringToObject(root, "message", message);
    }
    // The newline is appended below, so one byte is held back for it
    const bool printed = out_size >= 2 && cJSON_PrintPreallocated(root, out_line, static_cast<int>(out_size - 1), false);
    cJSON_Delete(root);
    if (!printed) {
        return 0;
    }
    const size_t length = strlen(out_line);
    out_line[length] = '\n';
    out_line[length + 1] = '\0';
    return length + 1;
}

size_t development_protocol_format_info(const char* vendor, const char* name, const char* id, const char* version, char* out_line, size_t out_size) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return 0;
    }
    cJSON_AddStringToObject(root, "status", "accepted");
    cJSON* info = cJSON_AddObjectToObject(root, "info");
    if (info != nullptr) {
        cJSON_AddStringToObject(info, "vendor", vendor);
        cJSON_AddStringToObject(info, "name", name);
        cJSON_AddStringToObject(info, "id", id);
        cJSON_AddStringToObject(info, "version", version);
    }
    const bool printed = info != nullptr && out_size >= 2 && cJSON_PrintPreallocated(root, out_line, static_cast<int>(out_size - 1), false);
    cJSON_Delete(root);
    if (!printed) {
        return 0;
    }
    const size_t length = strlen(out_line);
    out_line[length] = '\n';
    out_line[length + 1] = '\0';
    return length + 1;
}

DevelopmentFrameParseResult development_frame_parse(const uint8_t* data, size_t size, DevelopmentFrame* out_frame, size_t* out_consumed) {
    if (size < DEVELOPMENT_FRAME_HEADER_SIZE) {
        return DevelopmentFrameParseResult::Incomplete;
    }
    const uint16_t length = static_cast<uint16_t>((data[1] << 8) | data[2]);
    if (length > DEVELOPMENT_FRAME_PAYLOAD_MAX) {
        return DevelopmentFrameParseResult::Invalid;
    }
    if (size < DEVELOPMENT_FRAME_HEADER_SIZE + length) {
        return DevelopmentFrameParseResult::Incomplete;
    }
    out_frame->type = static_cast<DevelopmentFrameType>(data[0]);
    out_frame->payload = data + DEVELOPMENT_FRAME_HEADER_SIZE;
    out_frame->length = length;
    *out_consumed = DEVELOPMENT_FRAME_HEADER_SIZE + length;
    return DevelopmentFrameParseResult::Ok;
}

void development_frame_write_header(DevelopmentFrameType type, uint16_t length, uint8_t out_header[DEVELOPMENT_FRAME_HEADER_SIZE]) {
    out_header[0] = type;
    out_header[1] = static_cast<uint8_t>(length >> 8);
    out_header[2] = static_cast<uint8_t>(length & 0xFF);
}
