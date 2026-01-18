#include <Tactility/network/HttpdReq.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/StringUtils.h>
#include <Tactility/file/File.h>

#include <memory>
#include <ranges>
#include <sstream>

#ifdef ESP_PLATFORM

namespace tt::network {

static const auto LOGGER = Logger("HttpdReq");

bool getHeaderOrSendError(httpd_req_t* request, const std::string& name, std::string& value) {
    size_t header_size = httpd_req_get_hdr_value_len(request, name.c_str());
    if (header_size == 0) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "header missing");
        return false;
    }

    auto* raw_buffer = static_cast<char*>(malloc(header_size + 1));
    if (raw_buffer == nullptr) {
        LOGGER.error( LOG_MESSAGE_ALLOC_FAILED);
        httpd_resp_send_500(request);
        return false;
    }
    std::unique_ptr<char[], decltype(&free)> header_buffer(raw_buffer, free);

    if (httpd_req_get_hdr_value_str(request, name.c_str(), header_buffer.get(), header_size + 1) != ESP_OK) {
        httpd_resp_send_500(request);
        return false;
    }

    value = header_buffer.get();
    return true;
}

bool getMultiPartBoundaryOrSendError(httpd_req_t* request, std::string& boundary) {
    std::string content_type_header;
    if (!getHeaderOrSendError(request, "Content-Type", content_type_header)) {
        return false;
    }

    auto boundary_index = content_type_header.find("boundary=");
    if (boundary_index == std::string::npos) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "boundary not found in Content-Type");
        return false;
    }

    boundary = content_type_header.substr(boundary_index + 9);
    return true;
}

bool getQueryOrSendError(httpd_req_t* request, std::string& query) {
    size_t buffer_length = httpd_req_get_url_query_len(request);
    if (buffer_length == 0) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "id not specified");
        return false;
    }

    auto buffer = std::make_unique<char[]>(buffer_length + 1);
    if (buffer.get() == nullptr || httpd_req_get_url_query_str(request, buffer.get(), buffer_length + 1) != ESP_OK) {
        httpd_resp_send_500(request);
        return false;
    }

    query = buffer.get();

    return true;
}

std::unique_ptr<char[]> receiveByteArray(httpd_req_t* request, size_t length, size_t& bytesRead) {
    assert(length > 0);
    bytesRead = 0;

    // We have to use malloc() because make_unique() throws an exception
    // and we don't have exceptions enabled in the compiler settings
    auto* buffer = static_cast<char*>(malloc(length));
    if (buffer == nullptr) {
        LOGGER.error(LOG_MESSAGE_ALLOC_FAILED_FMT, length);
        return nullptr;
    }

    constexpr int MAX_TIMEOUT_RETRIES = 5;
    int timeout_retries = 0;
    while (bytesRead < length) {
        size_t read_size = length - bytesRead;
        int bytes_received = httpd_req_recv(request, buffer + bytesRead, read_size);
        if (bytes_received == HTTPD_SOCK_ERR_TIMEOUT) {
            // Timeout - retry with backoff
            timeout_retries++;
            if (timeout_retries >= MAX_TIMEOUT_RETRIES) {
                LOGGER.warn("Recv timeout after {} retries, read {}/{} bytes", timeout_retries, bytesRead, length);
                free(buffer);
                return nullptr;
            }
            LOGGER.warn("Recv timeout, retry {}/{}", timeout_retries, MAX_TIMEOUT_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(100 * timeout_retries)); // Exponential backoff
            continue;
        }
        if (bytes_received <= 0) {
            LOGGER.warn("Received error {} after reading {}/{} bytes", bytes_received, bytesRead, length);
            free(buffer);
            return nullptr;
        }

        // Successful read - reset timeout counter
        timeout_retries = 0;
        bytesRead += bytes_received;
    }

    return std::unique_ptr<char[]>(buffer);
}

std::string receiveTextUntil(httpd_req_t* request, const std::string& terminator) {
    std::string result;
    constexpr int MAX_TIMEOUT_RETRIES = 5;
    int timeout_retries = 0;
    while (result.size() < terminator.size() || 
           result.compare(result.size() - terminator.size(), terminator.size(), terminator) != 0) {
        char buffer;
        int bytes_read = httpd_req_recv(request, &buffer, 1);
        if (bytes_read == HTTPD_SOCK_ERR_TIMEOUT) {
            // Timeout - retry with backoff
            timeout_retries++;
            if (timeout_retries >= MAX_TIMEOUT_RETRIES) {
                LOGGER.warn("receiveTextUntil timeout after {} retries", timeout_retries);
                return "";
            }
            vTaskDelay(pdMS_TO_TICKS(100 * timeout_retries)); // Linear backoff
            continue;
        }
        if (bytes_read <= 0) {
            return "";
        }

        // Successful read - reset timeout counter
        timeout_retries = 0;
        result += buffer;
    }

    return result;
}

std::map<std::string, std::string> parseContentDisposition(const std::vector<std::string>& input) {
    std::map<std::string, std::string> result;
    static std::string prefix = "Content-Disposition: ";

    // Find header
    auto content_disposition_header = std::ranges::find_if(input, [](const std::string& header) {
        return header.starts_with(prefix);
    });

    // Header not found
    if (content_disposition_header == input.end()) {
        return result;
    }

    auto parseable = content_disposition_header->substr(prefix.size());
    auto parts = string::split(parseable, "; ");
    for (const auto& part : parts) {
        auto key_value = string::split(part, "=");
        if (key_value.size() == 2) {
            // Trim trailing newlines
            auto value = string::trim(key_value[1], "\r\n");
            if (value.size() > 2) {
                result[key_value[0]] = value.substr(1, value.size() - 2);
            } else {
                result[key_value[0]] = "";
            }
        }
    }

    return result;
}

bool readAndDiscardOrSendError(httpd_req_t* request, const std::string& toRead) {
    size_t bytes_read;
    auto buffer = receiveByteArray(request, toRead.length(), bytes_read);
    if (buffer == nullptr || bytes_read != toRead.length()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "failed to read discardable data");
        return false;
    }

    if (memcmp(buffer.get(), toRead.c_str(), bytes_read) != 0) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "discardable data mismatch");
        return false;
    }

    return true;
}

size_t receiveFile(httpd_req_t* request, size_t length, const std::string& filePath) {
    constexpr auto BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE];
    size_t bytes_received = 0;

    auto lock = file::getLock(filePath)->asScopedLock();
    lock.lock();

    auto* file = fopen(filePath.c_str(), "wb");
    if (file == nullptr) {
        LOGGER.error("Failed to open file for writing: {}", filePath);
        return 0;
    }

    constexpr int MAX_TIMEOUT_RETRIES = 5;
    int timeout_retries = 0;
    bool success = true;
    while (bytes_received < length) {
        auto expected_chunk_size = std::min<size_t>(BUFFER_SIZE, length - bytes_received);
        int receive_chunk_size = httpd_req_recv(request, buffer, expected_chunk_size);
        if (receive_chunk_size == HTTPD_SOCK_ERR_TIMEOUT) {
            // Timeout - retry with backoff
            timeout_retries++;
            if (timeout_retries >= MAX_TIMEOUT_RETRIES) {
                LOGGER.error("receiveFile timeout after {} retries, received {}/{} bytes", timeout_retries, bytes_received, length);
                success = false;
                break;
            }
            LOGGER.warn("receiveFile timeout, retry {}/{}", timeout_retries, MAX_TIMEOUT_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(100 << (timeout_retries - 1))); // Exponential backoff
            continue;
        }
        if (receive_chunk_size <= 0) {
            LOGGER.error("Receive failed with error {}", receive_chunk_size);
            success = false;
            break;
        }
        // Successful read - reset timeout counter
        timeout_retries = 0;
        if (fwrite(buffer, 1, receive_chunk_size, file) != (size_t)receive_chunk_size) {
            LOGGER.error("Failed to write all bytes");
            success = false;
            break;
        }
        bytes_received += receive_chunk_size;
    }

    // Write file
    fclose(file);
    if (!success) {
        remove(filePath.c_str());
        return 0;
    }
    return bytes_received;
}

}

#endif // ESP_PLATFORM