#include "doctest.h"

#include <mesh/mesh_store.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string makeTempDirectory() {
    char pattern[] = "/tmp/mesh_store_test_XXXXXX";
    return mkdtemp(pattern);
}

MeshMessage makeMessage(const char* text, bool outgoing, MeshDeliveryState delivery) {
    MeshMessage message {};
    strcpy(message.backend, "test");
    message.outgoing = outgoing;
    message.delivery = delivery;
    message.peer.length = 4;
    memcpy(message.peer.bytes, "\x01\x02\x03\x04", 4);
    message.channel = 2;
    strcpy(message.sender_name, "alice");
    message.wall_time = 1750000000;
    message.time_source = MESH_TIME_SOURCE_SYSTEM;
    message.uptime_ms = 1234;
    message.rssi = -80.5f;
    message.snr = 7.25f;
    strncpy(message.text, text, MESH_TEXT_MAX_LENGTH);
    return message;
}

} // namespace

TEST_CASE("MeshStore stores, reads back and survives a reopen") {
    const std::string directory = makeTempDirectory();

    {
        MeshStore store;
        CHECK_EQ(store.open(directory.c_str()), ERROR_NONE);
        auto first = makeMessage("hello", false, MESH_DELIVERY_RECEIVED);
        auto second = makeMessage("world", true, MESH_DELIVERY_QUEUED);
        CHECK_EQ(store.add(&first), ERROR_NONE);
        CHECK_EQ(store.add(&second), ERROR_NONE);
        CHECK_EQ(first.seq, 1);
        CHECK_EQ(second.seq, 2);

        // Read from the unflushed buffer
        MeshMessage read {};
        CHECK_EQ(store.get(1, &read), ERROR_NONE);
        CHECK_EQ(std::string(read.text), "hello");
        CHECK_EQ(std::string(read.sender_name), "alice");
        CHECK_EQ(read.peer.length, 4);
        CHECK_EQ(read.rssi, -80.5f);

        CHECK_EQ(store.setDelivery(2, MESH_DELIVERY_SENT), ERROR_NONE);
        CHECK_EQ(store.setDelivery(99, MESH_DELIVERY_SENT), ERROR_NOT_FOUND);
        store.close();
    }

    MeshStore reopened;
    CHECK_EQ(reopened.open(directory.c_str()), ERROR_NONE);
    CHECK_EQ(reopened.getMessageCount(), 2);

    MeshMessage read {};
    CHECK_EQ(reopened.get(2, &read), ERROR_NONE);
    CHECK_EQ(std::string(read.text), "world");
    CHECK(read.outgoing);
    CHECK_EQ(read.delivery, MESH_DELIVERY_SENT);
    CHECK_EQ(read.wall_time, 1750000000);

    // New messages continue the sequence
    auto third = makeMessage("again", false, MESH_DELIVERY_RECEIVED);
    CHECK_EQ(reopened.add(&third), ERROR_NONE);
    CHECK_EQ(third.seq, 3);
}

TEST_CASE("MeshStore history is newest first and pages by sequence number") {
    MeshStore store;
    CHECK_EQ(store.open(makeTempDirectory().c_str()), ERROR_NONE);
    for (int i = 0; i < 5; i++) {
        auto message = makeMessage(std::to_string(i).c_str(), false, MESH_DELIVERY_RECEIVED);
        CHECK_EQ(store.add(&message), ERROR_NONE);
    }

    MeshMessage page[2];
    CHECK_EQ(store.getHistory(0, page, 2), 2);
    CHECK_EQ(page[0].seq, 5);
    CHECK_EQ(page[1].seq, 4);
    CHECK_EQ(store.getHistory(page[1].seq, page, 2), 2);
    CHECK_EQ(page[0].seq, 3);
    CHECK_EQ(page[1].seq, 2);
    CHECK_EQ(store.getHistory(2, page, 2), 1);
    CHECK_EQ(page[0].seq, 1);
}

TEST_CASE("MeshStore deletes the oldest segment when over budget") {
    const MeshStore::Limits limits = { .bufferSize = 512, .segmentSize = 1024, .segmentCount = 2 };
    MeshStore store(limits);
    CHECK_EQ(store.open(makeTempDirectory().c_str()), ERROR_NONE);

    for (int i = 0; i < 100; i++) {
        auto message = makeMessage("a message that takes up some space in the log", false, MESH_DELIVERY_RECEIVED);
        CHECK_EQ(store.add(&message), ERROR_NONE);
    }
    store.flush();

    // Only the last segments are kept, so the oldest messages are gone
    MeshMessage message {};
    CHECK_EQ(store.get(1, &message), ERROR_NOT_FOUND);
    CHECK_EQ(store.get(100, &message), ERROR_NONE);
    CHECK_LT(store.getMessageCount(), 100);
}

TEST_CASE("MeshStore finds outgoing messages by backend and delivery state") {
    MeshStore store;
    CHECK_EQ(store.open(makeTempDirectory().c_str()), ERROR_NONE);
    auto incoming = makeMessage("in", false, MESH_DELIVERY_RECEIVED);
    auto blocked = makeMessage("out", true, MESH_DELIVERY_BLOCKED);
    auto other = makeMessage("other", true, MESH_DELIVERY_BLOCKED);
    strcpy(other.backend, "other");
    CHECK_EQ(store.add(&incoming), ERROR_NONE);
    CHECK_EQ(store.add(&blocked), ERROR_NONE);
    CHECK_EQ(store.add(&other), ERROR_NONE);

    const auto found = store.findOutgoing("test", MESH_DELIVERY_BLOCKED);
    REQUIRE_EQ(found.size(), 1);
    CHECK_EQ(found[0], blocked.seq);
}

TEST_CASE("MeshStore keeps records that are added after a torn record") {
    const std::string directory = makeTempDirectory();
    {
        MeshStore store;
        CHECK_EQ(store.open(directory.c_str()), ERROR_NONE);
        auto first = makeMessage("before", false, MESH_DELIVERY_RECEIVED);
        CHECK_EQ(store.add(&first), ERROR_NONE);
        store.close();
    }

    // A power loss during a flush leaves part of a record behind
    FILE* file = fopen((directory + "/seg-00000000.log").c_str(), "ab");
    REQUIRE_NE(file, nullptr);
    fwrite("\x4d\x53\x01", 1, 3, file);
    fclose(file);

    {
        MeshStore store;
        CHECK_EQ(store.open(directory.c_str()), ERROR_NONE);
        CHECK_EQ(store.getMessageCount(), 1);
        auto second = makeMessage("after", false, MESH_DELIVERY_RECEIVED);
        CHECK_EQ(store.add(&second), ERROR_NONE);
        CHECK_EQ(store.setDelivery(second.seq, MESH_DELIVERY_SENT), ERROR_NONE);
        store.close();
    }

    MeshStore reopened;
    CHECK_EQ(reopened.open(directory.c_str()), ERROR_NONE);
    CHECK_EQ(reopened.getMessageCount(), 2);
    MeshMessage read {};
    CHECK_EQ(reopened.get(2, &read), ERROR_NONE);
    CHECK_EQ(std::string(read.text), "after");
    CHECK_EQ(read.delivery, MESH_DELIVERY_SENT);
}

TEST_CASE("MeshStore keeps a full budget of segments when it recovers from a torn record") {
    const std::string directory = makeTempDirectory();
    const MeshStore::Limits limits = { .bufferSize = 512, .segmentSize = 1024, .segmentCount = 2 };
    size_t countBefore = 0;
    {
        MeshStore store(limits);
        CHECK_EQ(store.open(directory.c_str()), ERROR_NONE);
        for (int i = 0; i < 100; i++) {
            auto message = makeMessage("a message that takes up some space in the log", false, MESH_DELIVERY_RECEIVED);
            CHECK_EQ(store.add(&message), ERROR_NONE);
        }
        store.close();
    }

    // Find the newest segment and tear its last record
    std::string newest;
    {
        MeshStore store(limits);
        CHECK_EQ(store.open(directory.c_str()), ERROR_NONE);
        countBefore = store.getMessageCount();
        store.close();
    }
    for (int segment = 0; segment < 1000; segment++) {
        char name[32];
        snprintf(name, sizeof(name), "/seg-%08d.log", segment);
        if (FILE* existing = fopen((directory + name).c_str(), "rb")) {
            fclose(existing);
            newest = directory + name;
        }
    }
    REQUIRE_FALSE(newest.empty());
    FILE* file = fopen(newest.c_str(), "ab");
    REQUIRE_NE(file, nullptr);
    fwrite("\x4d\x53\x01", 1, 3, file);
    fclose(file);

    // Opening alone doesn't delete any messages
    {
        MeshStore store(limits);
        CHECK_EQ(store.open(directory.c_str()), ERROR_NONE);
        CHECK_EQ(store.getMessageCount(), countBefore);
        store.close();
    }
    MeshStore reopened(limits);
    CHECK_EQ(reopened.open(directory.c_str()), ERROR_NONE);
    CHECK_EQ(reopened.getMessageCount(), countBefore);

    // The budget applies again once new records are written
    auto message = makeMessage("after", false, MESH_DELIVERY_RECEIVED);
    CHECK_EQ(reopened.add(&message), ERROR_NONE);
    CHECK_EQ(reopened.flush(), ERROR_NONE);
    MeshMessage read {};
    CHECK_EQ(reopened.get(message.seq, &read), ERROR_NONE);
    CHECK_EQ(std::string(read.text), "after");
}

TEST_CASE("MeshStore reads the messages of one conversation") {
    const std::string directory = makeTempDirectory();
    const MeshAddress alice = { .length = 4, .bytes = { 1, 2, 3, 4 } };
    const MeshAddress bob = { .length = 4, .bytes = { 5, 6, 7, 8 } };

    const auto add = [](MeshStore& store, const char* backend, const MeshAddress* peer, uint8_t channel, const char* text) {
        auto message = makeMessage(text, false, MESH_DELIVERY_RECEIVED);
        strcpy(message.backend, backend);
        message.peer = (peer != nullptr) ? *peer : MeshAddress {};
        message.channel = channel;
        REQUIRE_EQ(store.add(&message), ERROR_NONE);
    };

    // The index is built from the log after a reopen, and from the buffer before it
    for (int round = 0; round < 2; round++) {
        MeshStore store;
        REQUIRE_EQ(store.open(directory.c_str()), ERROR_NONE);
        if (round == 0) {
            add(store, "test", &alice, 0, "alice 1");
            add(store, "test", nullptr, 0, "channel 0");
            add(store, "test", &bob, 0, "bob 1");
            add(store, "test", &alice, 0, "alice 2");
            add(store, "other", &alice, 0, "other backend");
            add(store, "test", nullptr, 3, "channel 3");
        }

        MeshMessage messages[8];
        REQUIRE_EQ(store.getConversation("test", &alice, 0, 0, messages, 8), 2);
        CHECK_EQ(std::string(messages[0].text), "alice 2");
        CHECK_EQ(std::string(messages[1].text), "alice 1");

        REQUIRE_EQ(store.getConversation("test", nullptr, 3, 0, messages, 8), 1);
        CHECK_EQ(std::string(messages[0].text), "channel 3");
        REQUIRE_EQ(store.getConversation("test", nullptr, 0, 0, messages, 8), 1);
        CHECK_EQ(std::string(messages[0].text), "channel 0");
        CHECK_EQ(store.getConversation("test", nullptr, 7, 0, messages, 8), 0);

        REQUIRE_EQ(store.getConversation("other", &alice, 0, 0, messages, 8), 1);
        CHECK_EQ(std::string(messages[0].text), "other backend");

        // Paging back from a message
        REQUIRE_EQ(store.getConversation("test", &alice, 0, 4, messages, 8), 1);
        CHECK_EQ(std::string(messages[0].text), "alice 1");
        CHECK_EQ(store.getConversation("test", &alice, 0, 0, messages, 1), 1);
        store.close();
    }
}

TEST_CASE("MeshStore reads messages oldest first after a sequence number") {
    const std::string directory = makeTempDirectory();
    MeshStore store;
    REQUIRE_EQ(store.open(directory.c_str()), ERROR_NONE);
    for (const char* text : { "one", "two", "three" }) {
        MeshMessage message = makeMessage(text, false, MESH_DELIVERY_RECEIVED);
        REQUIRE_EQ(store.add(&message), ERROR_NONE);
    }
    CHECK(store.isOpen());

    MeshMessage messages[4];
    REQUIRE_EQ(store.getAfter(0, messages, 4), 3);
    CHECK_EQ(std::string(messages[0].text), "one");
    CHECK_EQ(std::string(messages[2].text), "three");

    REQUIRE_EQ(store.getAfter(messages[0].seq, messages, 1), 1);
    CHECK_EQ(std::string(messages[0].text), "two");
    CHECK_EQ(store.getAfter(messages[0].seq + 1, messages, 4), 0);

    store.close();
    CHECK_FALSE(store.isOpen());
}
