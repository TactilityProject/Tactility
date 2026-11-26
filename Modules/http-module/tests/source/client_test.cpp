#include "doctest.h"

#include <http/client.h>
#include <http/private/http_parsing.h>

TEST_CASE("http_parse_url accepts http and https") {
    HttpUrl url;
    REQUIRE(http_parse_url("http://example.org/a?b", url));
    CHECK_FALSE(url.secure);
    CHECK_EQ(url.port, "80");
    CHECK_EQ(url.path, "/a?b");
    REQUIRE(http_parse_url("https://example.org", url));
    CHECK(url.secure);
    CHECK_EQ(url.port, "443");
}

TEST_CASE("http_parse_url rejects characters that would split the request") {
    HttpUrl url;
    CHECK_FALSE(http_parse_url("http://example.org/a b", url));
    CHECK_FALSE(http_parse_url("http://example.org/a\r\nHost: x", url));
    CHECK_FALSE(http_parse_url("ftp://example.org/", url));
}

TEST_CASE("http_client rejects bad headers and URLs") {
    HttpClient* client = http_client_alloc();
    REQUIRE(client != nullptr);
    CHECK_EQ(http_client_set_header(client, "Accept", "text/html"), ERROR_NONE);
    CHECK_EQ(http_client_set_header(client, "X", "a\r\nb"), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(http_client_set_header(client, "Host", "x"), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(http_client_open(client, "file:///etc/passwd"), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(http_client_get_failure(client), HTTP_CLIENT_FAILURE_URL);
    char buffer[4];
    CHECK_EQ(http_client_read(client, buffer, sizeof(buffer)), -1);
    CHECK_EQ(http_client_get_failure(client), HTTP_CLIENT_FAILURE_NOT_OPEN);
    http_client_free(client);
}
