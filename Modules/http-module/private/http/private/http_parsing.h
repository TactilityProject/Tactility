// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>

struct HttpUrl {
    std::string host;
    std::string port;
    /** The path and query, "/" when the URL has none */
    std::string path;
    /** True for https:// */
    bool secure = true;
};

/** Parses an http:// or https:// URL. The port is 80 or 443 when the URL has none. */
bool http_parse_url(const std::string& url, HttpUrl& out);

/** True when the name can be sent as a request header name */
bool http_is_valid_header_name(const char* name);

/** True when the value has no line breaks or other control characters */
bool http_is_valid_header_value(const char* value);

/** Parses an https:// URL. The port is 443 when the URL has none. */
bool http_parse_https_url(const std::string& url, HttpUrl& out);

/** Parses a status line such as "HTTP/1.1 200 OK" */
bool http_parse_status_line(const std::string& line, int& statusCode);

/** Parses a header line such as "Content-Length: 42". The name is returned in lower case, the value without surrounding whitespace. */
bool http_parse_header_line(const std::string& line, std::string& name, std::string& value);

/** Parses a Content-Length value: decimal digits only, without sign or overflow */
bool http_parse_content_length(const std::string& value, long& length);

/** True when the last coding of a Transfer-Encoding value is "chunked", in any case */
bool http_is_chunked(const std::string& transferEncoding);

/** Parses the size line of a chunk in a chunked body, e.g. "1a2b" or "1a2b;name=value" */
bool http_parse_chunk_size(const std::string& line, size_t& size);
