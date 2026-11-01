#include "doctest.h"

#include <development/protocol.h>

#include <cstring>
#include <string>

namespace {

DevelopmentHelloError parse(const std::string& json, DevelopmentHello& hello) {
    const char* message = nullptr;
    const DevelopmentHelloError error = development_protocol_parse_hello(json.c_str(), json.size(), &hello, &message);
    CHECK_EQ(error == DevelopmentHelloError::None, message == nullptr);
    return error;
}

} // namespace

TEST_CASE("development_protocol_parse_hello accepts a shell hello") {
    DevelopmentHello hello {};
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"shell","client":"ken@laptop","columns":120,"rows":40})", hello), DevelopmentHelloError::None);
    CHECK_EQ(hello.protocol, 1);
    CHECK_EQ(std::string(hello.mode), "shell");
    CHECK_EQ(std::string(hello.client), "ken@laptop");
    CHECK_EQ(hello.columns, 120);
    CHECK_EQ(hello.rows, 40);
}

TEST_CASE("development_protocol_parse_hello accepts sync and app hellos") {
    DevelopmentHello hello {};
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"sync","client":"c"})", hello), DevelopmentHelloError::None);
    CHECK_EQ(std::string(hello.mode), "sync");
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"app","client":"c"})", hello), DevelopmentHelloError::None);
    CHECK_EQ(std::string(hello.mode), "app");
}

TEST_CASE("development_protocol_parse_hello accepts an info hello") {
    DevelopmentHello hello {};
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"info","client":"c"})", hello), DevelopmentHelloError::None);
    CHECK_EQ(std::string(hello.mode), "info");
    CHECK_EQ(parse(R"({"protocol":1,"mode":"info","client":"c","command":"echo hi"})", hello), DevelopmentHelloError::Malformed);
}

TEST_CASE("development_protocol_format_info writes the device information") {
    char line[256];
    const size_t length = development_protocol_format_info("LilyGO", "T-Deck", "lilygo-tdeck", "0.8.0", line, sizeof(line));
    CHECK_EQ(std::string(line, length), "{\"status\":\"accepted\",\"info\":{\"vendor\":\"LilyGO\",\"name\":\"T-Deck\",\"id\":\"lilygo-tdeck\",\"version\":\"0.8.0\"}}\n");
    CHECK_EQ(development_protocol_format_info("a", "b", "c", "d", line, 10), 0);
}

TEST_CASE("development_protocol_format_info fits exactly in the smallest buffer that holds the line and its terminator") {
    char probe[512];
    const size_t length = development_protocol_format_info("LilyGO", "T-Deck", "lilygo-tdeck", "0.8.0", probe, sizeof(probe));
    REQUIRE_GT(length, 0);
    for (size_t size = 2; size < length + 4; size++) {
        std::string guarded(size + 8, 'x');
        const size_t written = development_protocol_format_info("LilyGO", "T-Deck", "lilygo-tdeck", "0.8.0", guarded.data(), size);
        INFO("size ", size);
        CHECK_EQ(written > 0, size >= length + 1);
        CHECK_EQ(guarded.substr(size), std::string(8, 'x'));
    }
}

TEST_CASE("development_protocol_parse_hello accepts a command in shell mode only") {
    DevelopmentHello hello {};
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"shell","client":"c","command":"echo hi"})", hello), DevelopmentHelloError::None);
    CHECK_EQ(std::string(hello.command), "echo hi");
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"shell","client":"c"})", hello), DevelopmentHelloError::None);
    CHECK_EQ(std::string(hello.command), "");
    CHECK_EQ(parse(R"({"protocol":1,"mode":"sync","client":"c","command":"echo hi"})", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"protocol":1,"mode":"shell","client":"c","command":3})", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"protocol":1,"mode":"shell","client":"c","command":")" + std::string(DEVELOPMENT_COMMAND_LENGTH + 1, 'x') + "\"}", hello), DevelopmentHelloError::Malformed);
}

TEST_CASE("development_protocol_parse_hello defaults a missing or invalid window size") {
    DevelopmentHello hello {};
    REQUIRE_EQ(parse(R"({"protocol":1,"mode":"shell","client":"c","columns":0})", hello), DevelopmentHelloError::None);
    CHECK_EQ(hello.columns, 80);
    CHECK_EQ(hello.rows, 24);
}

TEST_CASE("development_protocol_parse_hello rejects invalid hellos") {
    DevelopmentHello hello {};
    CHECK_EQ(parse("not json", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse("[]", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"mode":"shell","client":"c"})", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"protocol":1,"client":"c"})", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"protocol":1,"mode":"shell"})", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"protocol":1,"mode":"shell","client":")" + std::string(DEVELOPMENT_CLIENT_NAME_LENGTH + 1, 'x') + "\"}", hello), DevelopmentHelloError::Malformed);
    CHECK_EQ(parse(R"({"protocol":2,"mode":"shell","client":"c"})", hello), DevelopmentHelloError::UnsupportedProtocol);
    CHECK_EQ(parse(R"({"protocol":1,"mode":"ftp","client":"c"})", hello), DevelopmentHelloError::UnsupportedMode);
}

TEST_CASE("development_protocol_format_status writes a JSON line") {
    char line[64];
    const size_t length = development_protocol_format_status("error", "busy", line, sizeof(line));
    CHECK_EQ(std::string(line, length), "{\"status\":\"error\",\"message\":\"busy\"}\n");

    CHECK_EQ(development_protocol_format_status("accepted", nullptr, line, sizeof(line)), strlen("{\"status\":\"accepted\"}\n"));
    CHECK_EQ(development_protocol_format_status("accepted", nullptr, line, 8), 0);
}

TEST_CASE("development_frame_parse handles complete, partial and oversized frames") {
    uint8_t data[DEVELOPMENT_FRAME_HEADER_SIZE + 3 + DEVELOPMENT_FRAME_HEADER_SIZE];
    development_frame_write_header(DEVELOPMENT_FRAME_DATA, 3, data);
    memcpy(data + DEVELOPMENT_FRAME_HEADER_SIZE, "abc", 3);
    development_frame_write_header(DEVELOPMENT_FRAME_EXIT, 0, data + DEVELOPMENT_FRAME_HEADER_SIZE + 3);

    DevelopmentFrame frame {};
    size_t consumed = 0;
    CHECK_EQ(development_frame_parse(data, 2, &frame, &consumed), DevelopmentFrameParseResult::Incomplete);
    CHECK_EQ(development_frame_parse(data, DEVELOPMENT_FRAME_HEADER_SIZE + 2, &frame, &consumed), DevelopmentFrameParseResult::Incomplete);

    REQUIRE_EQ(development_frame_parse(data, sizeof(data), &frame, &consumed), DevelopmentFrameParseResult::Ok);
    CHECK_EQ(frame.type, DEVELOPMENT_FRAME_DATA);
    CHECK_EQ(std::string(reinterpret_cast<const char*>(frame.payload), frame.length), "abc");
    CHECK_EQ(consumed, DEVELOPMENT_FRAME_HEADER_SIZE + 3);

    REQUIRE_EQ(development_frame_parse(data + consumed, sizeof(data) - consumed, &frame, &consumed), DevelopmentFrameParseResult::Ok);
    CHECK_EQ(frame.type, DEVELOPMENT_FRAME_EXIT);
    CHECK_EQ(frame.length, 0);
    CHECK_EQ(consumed, DEVELOPMENT_FRAME_HEADER_SIZE);

    uint8_t oversized[DEVELOPMENT_FRAME_HEADER_SIZE];
    development_frame_write_header(DEVELOPMENT_FRAME_DATA, DEVELOPMENT_FRAME_PAYLOAD_MAX + 1, oversized);
    CHECK_EQ(development_frame_parse(oversized, sizeof(oversized), &frame, &consumed), DevelopmentFrameParseResult::Invalid);
}
