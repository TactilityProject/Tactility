// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/error.h>

#include <cstddef>
#include <cstdint>

/**
 * Durable message log: records are appended to a RAM buffer and flushed to numbered segment files.
 * When the segment count exceeds its budget, the oldest segment is deleted. The index lives in RAM and
 * is rebuilt at open() by scanning the record headers. Not thread-safe.
 */
class MeshStore final {
public:
    struct Limits {
        size_t bufferSize;
        size_t segmentSize;
        size_t segmentCount;
    };

    static constexpr Limits DEFAULT_LIMITS = { .bufferSize = 8 * 1024, .segmentSize = 32 * 1024, .segmentCount = 16 };

private:
    struct IndexEntry {
        uint32_t seq;
        /** Segment number, or PENDING for records still in the buffer */
        uint32_t segment;
        uint32_t offset;
        enum MeshDeliveryState delivery;
        /** Channel index, meaningful for messages without a peer */
        uint8_t channel;
        /** Hash of the peer address, 0 for channel messages */
        uint32_t peerHash;
    };

    static constexpr uint32_t PENDING = UINT32_MAX;

    Limits limits;
    tt::std::string directory;
    uint8_t* buffer = nullptr;
    size_t bufferUsed = 0;
    tt::std::vector<IndexEntry> index;
    tt::std::vector<uint32_t> segments;
    uint32_t currentSegmentSize = 0;
    uint32_t nextSeq = 1;
    uint64_t firstPendingMs = 0;

    tt::std::string getSegmentPath(uint32_t segment) const;
    /** @return the end offset of the last valid record */
    uint32_t scanSegment(uint32_t segment);
    static IndexEntry makeIndexEntry(const MeshMessage& message, uint32_t segment, uint32_t offset);
    void applyRecord(uint8_t type, const uint8_t* payload, size_t length, uint32_t segment, uint32_t offset);
    error_t appendRecord(uint8_t type, const uint8_t* payload, size_t length, uint32_t seq);
    IndexEntry* findEntry(uint32_t seq);
    const IndexEntry* findEntry(uint32_t seq) const;
    error_t readEntry(const IndexEntry& entry, MeshMessage* message) const;
    void enforceBudget();

public:
    explicit MeshStore(const Limits& limits = DEFAULT_LIMITS);
    ~MeshStore();

    MeshStore(const MeshStore&) = delete;
    MeshStore& operator=(const MeshStore&) = delete;

    /** Open the store in @a directory (created when missing) and rebuild the index */
    error_t open(const char* directory);

    /** Flush and close */
    void close();

    /** Store a new message. Its seq is assigned by the store. */
    error_t add(MeshMessage* message);

    error_t setDelivery(uint32_t seq, enum MeshDeliveryState delivery);

    error_t get(uint32_t seq, MeshMessage* message) const;

    bool isOpen() const { return buffer != nullptr; }

    /** Oldest first: the messages with a seq above @a afterSeq */
    size_t getAfter(uint32_t afterSeq, MeshMessage* messages, size_t maxCount) const;

    /** Newest first, see mesh_history_get() */
    size_t getHistory(uint32_t beforeSeq, MeshMessage* messages, size_t maxCount) const;

    /**
     * Newest first, like getHistory(), limited to the messages of one conversation of @a backend.
     * @param[in] peer the peer of a direct conversation, or nullptr for the conversation of @a channel
     */
    size_t getConversation(const char* backend, const MeshAddress* peer, uint8_t channel, uint32_t beforeSeq, MeshMessage* messages, size_t maxCount) const;

    /** Sequence numbers of outgoing messages of @a backend in the given delivery state, oldest first */
    tt::std::vector<uint32_t> findOutgoing(const char* backend, enum MeshDeliveryState delivery) const;

    /** Write the buffer to the current segment */
    error_t flush();

    /** Flush when the buffer is mostly full or holds records older than @a maxAgeMs */
    void flushIfNeeded(uint64_t nowMs, uint64_t maxAgeMs);

    size_t getMessageCount() const { return index.size(); }
};
