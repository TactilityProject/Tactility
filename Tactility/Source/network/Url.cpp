#include "Tactility/network/Url.h"

namespace tt::network {

std::map<std::string, std::string> parseUrlQuery(std::string query) {
    std::map<std::string, std::string> result;

    if (query.empty()) {
        return result;
    }

    size_t current_index = query[0] == '?' ? 1U : 0U;
    auto equals_index = query.find_first_of('=', current_index);
    while (equals_index != std::string::npos) {
        auto index_boundary = query.find_first_of('&', equals_index + 1);
        if (index_boundary == std::string::npos) {
            index_boundary = query.size();
        }
        auto key = query.substr(current_index, (equals_index - current_index));
        auto decodedKey = urlDecode(key);
        auto value = query.substr(equals_index + 1, (index_boundary - equals_index - 1));
        auto decodedValue = urlDecode(value);

        result[decodedKey] = decodedValue;

        // Find next token
        current_index = index_boundary + 1;
        equals_index = query.find_first_of('=', current_index);
    }

    return result;
}

// Adapted from https://stackoverflow.com/a/29962178/3848666
std::string urlEncode(const std::string& input) {
    std::string result = "";
    const char* characters = input.c_str();
    char hex_buffer[10];
    size_t input_length = input.length();

    for (size_t i = 0;i < input_length;i++) {
        unsigned char c = characters[i];
        // uncomment this if you want to encode spaces with +
        if (c==' ') {
            result += '+';
        } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            snprintf(hex_buffer, sizeof(hex_buffer), "%%%02X", c); //%% means '%' literal, %02X means at least two digits, paddable with a leading zero
            result += hex_buffer;
        }
    }

    return result;
}

// Adapted from https://stackoverflow.com/a/29962178/3848666
std::string urlDecode(const std::string& input) {
    std::string result;
    size_t conversion_buffer, input_length = input.length();

    for (size_t i = 0; i < input_length; i++) {
        if (input[i] != '%') {
            if (input[i] == '+') {
                result += ' ';
            } else {
                result += input[i];
            }
        } else {
            sscanf(input.substr(i + 1, 2).c_str(), "%zx", &conversion_buffer);
            char c = static_cast<char>(conversion_buffer);
            result += c;
            i = i + 2;
        }
    }

    return result;
}

} // namespace
