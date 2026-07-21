#include "Tactility/StringUtils.h"

#include <cstring>
#include <ranges>
#include <sstream>
#include <string>

namespace tt::string {

bool getPathParent(const std::string& path, std::string& output) {
    auto index = path.find_last_of('/');
    if (index == std::string::npos) {
        return false;
    } else if (index == 0) {
        output = "/";
        return true;
    } else {
        output = path.substr(0, index);
        return true;
    }
}

std::string getLastPathSegment(const std::string& path) {
    auto index = path.find_last_of('/');
    if (index != std::string::npos) {
        return path.substr(index + 1);
    } else {
        return "";
    }
}

void split(const std::string& input, const std::string& delimiter, std::function<void(const std::string&)> callback) {
    size_t token_index = 0;
    size_t delimiter_index;
    const size_t delimiter_length = delimiter.length();

    while ((delimiter_index = input.find(delimiter, token_index)) != std::string::npos) {
        std::string token = input.substr(token_index, delimiter_index - token_index);
        token_index = delimiter_index + delimiter_length;
        callback(token);
    }

    auto end_token = input.substr(token_index);
    if (!end_token.empty()) {
        callback(end_token);
    }
}

std::vector<std::string> split(const std::string&input, const std::string&delimiter) {
    std::vector<std::string> result;
    split(input, delimiter, [&result](const std::string& token) {
        result.push_back(token);
    });
    return result;
}

std::string join(const std::vector<const char*>& input, const std::string& delimiter) {
    std::stringstream stream;
    size_t size = input.size();

    if (size == 0) {
        return "";
    } else if (size == 1) {
        return input.front();
    } else {
        auto iterator = input.begin();
        while (iterator != input.end()) {
            stream << *iterator;
            iterator++;
            if (iterator != input.end()) {
                stream << delimiter;
            }
        }
    }

    return stream.str();
}

std::string join(const std::vector<std::string>& input, const std::string& delimiter) {
    std::stringstream stream;
    size_t size = input.size();

    if (size == 0) {
        return "";
    } else if (size == 1) {
        return input.front();
    } else {
        auto iterator = input.begin();
        while (iterator != input.end()) {
            stream << *iterator;
            iterator++;
            if (iterator != input.end()) {
                stream << delimiter;
            }
        }
    }

    return stream.str();
}

std::string removeFileExtension(const std::string& input) {
    auto index = input.find('.');
    if (index != std::string::npos) {
        return input.substr(0, index);
    } else {
        return input;
    }
}

bool isAsciiHexString(const std::string& input) {
    // Find invalid characters
    return std::ranges::views::filter(input, [](char character){
        if (
           (('0' <= character) && ('9' >= character)) ||
           (('a' <= character) && ('f' >= character)) ||
           (('A' <= character) && ('F' >= character))
        ) {
            return false;
        } else {
            return true;
        }
    }).empty();
}

std::string trim(const std::string& input, const std::string& characters) {
    auto index = input.find_first_not_of(characters);
    if (index == std::string::npos) {
        return "";
    } else {
        auto end_index = input.find_last_not_of(characters);
        return input.substr(index, end_index - index + 1);
    }
}

} // namespace
