// SPDX-License-Identifier: Apache-2.0
#include <http/private/http_parsing.h>

#include <cctype>
#include <climits>
#include <cstdlib>
#include <string_view>

namespace {

std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t");
    return text.substr(start, end - start + 1);
}

bool is_digits(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

}

bool http_parse_https_url(const std::string& url, HttpUrl& out) {
    HttpUrl parsed;
    if (!http_parse_url(url, parsed) || !parsed.secure) {
        return false;
    }
    out = parsed;
    return true;
}

bool http_parse_url(const std::string& url, HttpUrl& out) {
    // Spaces and control characters would break the request line apart
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= ' ' || c == 127) {
            return false;
        }
    }
    const bool secure = url.compare(0, 8, "https://") == 0;
    if (!secure && url.compare(0, 7, "http://") != 0) {
        return false;
    }
    const std::string_view scheme = secure ? "https://" : "http://";

    const auto authorityStart = scheme.size();
    const auto pathStart = url.find_first_of("/?#", authorityStart);
    const std::string authority = url.substr(authorityStart, pathStart == std::string::npos ? std::string::npos : pathStart - authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        return false;
    }

    // An IPv6 address is enclosed in brackets, as it contains colons itself
    std::string host;
    std::string rest;
    if (authority[0] == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            return false;
        }
        host = authority.substr(1, close - 1);
        rest = authority.substr(close + 1);
    } else {
        const auto colon = authority.find(':');
        host = authority.substr(0, colon);
        rest = colon == std::string::npos ? "" : authority.substr(colon);
    }
    if (host.empty()) {
        return false;
    }

    std::string port = secure ? "443" : "80";
    if (!rest.empty()) {
        if (rest[0] != ':' || !is_digits(rest.substr(1))) {
            return false;
        }
        port = rest.substr(1);
    }

    std::string path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    // The fragment isn't sent to the server
    path = path.substr(0, path.find('#'));
    if (path.empty() || path[0] != '/') {
        path.insert(0, "/");
    }

    out = { .host = host, .port = port, .path = path, .secure = secure };
    return true;
}

bool http_is_valid_header_name(const char* name) {
    if (name == nullptr || *name == '\0') {
        return false;
    }
    for (const char* c = name; *c != '\0'; c++) {
        if (static_cast<unsigned char>(*c) <= ' ' || static_cast<unsigned char>(*c) >= 127 || *c == ':') {
            return false;
        }
    }
    return true;
}

bool http_is_valid_header_value(const char* value) {
    if (value == nullptr) {
        return false;
    }
    for (const char* c = value; *c != '\0'; c++) {
        if ((static_cast<unsigned char>(*c) < ' ' && *c != '\t') || *c == 127) {
            return false;
        }
    }
    return true;
}

bool http_parse_status_line(const std::string& line, int& statusCode) {
    if (line.compare(0, 5, "HTTP/") != 0) {
        return false;
    }
    const auto space = line.find(' ');
    if (space == std::string::npos) {
        return false;
    }
    const std::string code = line.substr(space + 1, 3);
    if (code.size() != 3 || !is_digits(code) || (line.size() > space + 4 && line[space + 4] != ' ')) {
        return false;
    }
    statusCode = std::atoi(code.c_str());
    return true;
}

bool http_parse_header_line(const std::string& line, std::string& name, std::string& value) {
    const auto colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    name = trim(line.substr(0, colon));
    for (auto& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    value = trim(line.substr(colon + 1));
    return !name.empty();
}

bool http_parse_content_length(const std::string& value, long& length) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    long result = 0;
    for (const char c : value) {
        const long digit = c - '0';
        if (result > (LONG_MAX - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    length = result;
    return true;
}

bool http_is_chunked(const std::string& transferEncoding) {
    const auto comma = transferEncoding.rfind(',');
    std::string coding = trim(comma == std::string::npos ? transferEncoding : transferEncoding.substr(comma + 1));
    for (auto& c : coding) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return coding == "chunked";
}

bool http_parse_chunk_size(const std::string& line, size_t& size) {
    const std::string digits = trim(line.substr(0, line.find(';')));
    if (digits.empty() || digits.size() > sizeof(size_t) * 2) {
        return false;
    }
    size_t result = 0;
    for (const char c : digits) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        result = result * 16 + static_cast<size_t>(lower <= '9' ? lower - '0' : lower - 'a' + 10);
    }
    // Larger sizes would turn negative in the signed counters of the readers
    if (result > static_cast<unsigned long long>(LLONG_MAX)) {
        return false;
    }
    size = result;
    return true;
}
