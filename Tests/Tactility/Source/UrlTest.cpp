#include "doctest.h"
#include <Tactility/network/Url.h>

using namespace tt;

TEST_CASE("parseUrlQuery can handle a single key-value pair") {
    auto map = network::parseUrlQuery("?key=value");
    CHECK_EQ(map.size(), 1);
    CHECK_EQ(map["key"], "value");
}

TEST_CASE("parseUrlQuery can handle empty value in the middle") {
    auto map = network::parseUrlQuery("?a=1&b=&c=3");
    CHECK_EQ(map.size(), 3);
    CHECK_EQ(map["a"], "1");
    CHECK_EQ(map["b"], "");
    CHECK_EQ(map["c"], "3");
}

TEST_CASE("parseUrlQuery can handle empty value at the end") {
    auto map = network::parseUrlQuery("?a=1&b=");
    CHECK_EQ(map.size(), 2);
    CHECK_EQ(map["a"], "1");
    CHECK_EQ(map["b"], "");
}

TEST_CASE("parseUrlQuery returns empty map when query s questionmark with a key without a value") {
    auto map = network::parseUrlQuery("?a");
    CHECK_EQ(map.size(), 0);
}

TEST_CASE("parseUrlQuery returns empty map when query is a questionmark") {
    auto map = network::parseUrlQuery("?");
    CHECK_EQ(map.size(), 0);
}

TEST_CASE("parseUrlQuery should url-decode the value") {
    auto map = network::parseUrlQuery("?key=Test%21Test");
    CHECK_EQ(map.size(), 1);
    CHECK_EQ(map["key"], "Test!Test");
}

TEST_CASE("parseUrlQuery should url-decode the key") {
    auto map = network::parseUrlQuery("?Test%21Test=value");
    CHECK_EQ(map.size(), 1);
    CHECK_EQ(map["Test!Test"], "value");
}

TEST_CASE("urlDecode") {
    auto input = std::string("prefix!*'();:@&=+$,/?#[]<>%-.^_`{}|~ \\");
    auto expected = std::string("prefix%21%2A%27%28%29%3B%3A%40%26%3D%2B%24%2C%2F%3F%23%5B%5D%3C%3E%25-.%5E_%60%7B%7D%7C~+%5C");
    auto encoded = network::urlEncode(input);
    CHECK_EQ(encoded, expected);
}

TEST_CASE("urlDecode") {
    auto input = std::string("prefix%21%2A%27%28%29%3B%3A%40%26%3D%2B%24%2C%2F%3F%23%5B%5D%3C%3E%25-.%5E_%60%7B%7D%7C~+%5C");
    auto expected = std::string("prefix!*'();:@&=+$,/?#[]<>%-.^_`{}|~ \\");
    auto decoded = network::urlDecode(input);
    CHECK_EQ(decoded, expected);
}