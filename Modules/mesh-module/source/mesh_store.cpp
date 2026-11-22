// SPDX-License-Identifier: Apache-2.0
#include <mesh/mesh_store.h>

#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/time.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>


constexpr auto* TAG = "mesh_store";

namespace {

constexpr size_t SCAN_BUFFER_SIZE = 4096;

// Record layout: magic (2) + type (1) + reserved (1) + payload length (2, LE) + payload
constexpr uint8_t RECORD_MAGIC_0 = 'M';
constexpr uint8_t RECORD_MAGIC_1 = 'S';
constexpr size_t RECORD_HEADER_SIZE = 6;
constexpr uint8_t RECORD_TYPE_MESSAGE = 1;
constexpr uint8_t RECORD_TYPE_DELIVERY = 2;
constexpr uint8_t RECORD_TYPE_DELETE_CONVERSATION = 3;
constexpr size_t MESSAGE_PAYLOAD_MAX_SIZE = 512;
constexpr auto* SEGMENT_PREFIX = "seg-";
constexpr auto* SEGMENT_SUFFIX = ".log";

bool isSameAddress(const MeshAddress& a, const MeshAddress& b) {
    return a.length == b.length && memcmp(a.bytes, b.bytes, a.length) == 0;
}

/**
 * FNV-1a, never 0 for an address so that 0 can mean "no peer". It has 64 bits, so the index can tell conversations
 * apart without reading the stored messages.
 */
uint64_t hashPeer(const MeshAddress& address) {
    if (address.length == 0) {
        return 0;
    }
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < address.length; i++) {
        hash = (hash ^ address.bytes[i]) * 1099511628211ull;
    }
    return hash | 1;
}

uint8_t* allocateBuffer(size_t size) {
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    return static_cast<uint8_t*>(memory_alloc_with_policy(size, &policy));
}

class Writer {
    uint8_t* data;
    size_t capacity;
    size_t position = 0;

public:
    Writer(uint8_t* data, size_t capacity) : data(data), capacity(capacity) {}

    void bytes(const void* source, size_t length) {
        if (position + length <= capacity) {
            memcpy(data + position, source, length);
        }
        position += length;
    }

    void u8(uint8_t value) { bytes(&value, 1); }

    void u32(uint32_t value) {
        const uint8_t encoded[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
        bytes(encoded, 4);
    }

    void u64(uint64_t value) {
        u32((uint32_t)value);
        u32((uint32_t)(value >> 32));
    }

    void f32(float value) {
        uint32_t raw;
        memcpy(&raw, &value, 4);
        u32(raw);
    }

    void string(const char* value, size_t maxLength) {
        const size_t length = strnlen(value, maxLength);
        u8((uint8_t)length);
        bytes(value, length);
    }

    size_t size() const { return position; }
    bool overflowed() const { return position > capacity; }
};

class Reader {
    const uint8_t* data;
    size_t length;
    size_t position = 0;
    bool failed = false;

public:
    Reader(const uint8_t* data, size_t length) : data(data), length(length) {}

    void bytes(void* target, size_t count) {
        if (position + count > length) {
            failed = true;
            memset(target, 0, count);
            return;
        }
        memcpy(target, data + position, count);
        position += count;
    }

    uint8_t u8() {
        uint8_t value;
        bytes(&value, 1);
        return value;
    }

    uint32_t u32() {
        uint8_t encoded[4];
        bytes(encoded, 4);
        return encoded[0] | (encoded[1] << 8) | (encoded[2] << 16) | ((uint32_t)encoded[3] << 24);
    }

    uint64_t u64() {
        const uint64_t low = u32();
        return low | ((uint64_t)u32() << 32);
    }

    float f32() {
        const uint32_t raw = u32();
        float value;
        memcpy(&value, &raw, 4);
        return value;
    }

    void string(char* target, size_t maxLength) {
        const size_t stringLength = u8();
        if (stringLength > maxLength) {
            failed = true;
            target[0] = '\0';
            return;
        }
        bytes(target, stringLength);
        target[stringLength] = '\0';
    }

    bool ok() const { return !failed; }
};

size_t serializeMessage(const MeshMessage& message, uint8_t* payload, size_t capacity) {
    Writer writer(payload, capacity);
    writer.u32(message.seq);
    writer.string(message.backend, MESH_BACKEND_ID_MAX_LENGTH);
    writer.u8(message.outgoing ? 1 : 0);
    writer.u8((uint8_t)message.delivery);
    writer.u8(message.peer.length);
    writer.bytes(message.peer.bytes, std::min<size_t>(message.peer.length, MESH_ADDRESS_MAX_LENGTH));
    writer.u8(message.channel);
    writer.string(message.sender_name, MESH_NAME_MAX_LENGTH);
    writer.u64((uint64_t)message.wall_time);
    writer.u8((uint8_t)message.time_source);
    writer.u64(message.uptime_ms);
    writer.f32(message.rssi);
    writer.f32(message.snr);
    writer.string(message.text, MESH_TEXT_MAX_LENGTH);
    return writer.overflowed() ? 0 : writer.size();
}

bool deserializeMessage(const uint8_t* payload, size_t length, MeshMessage* message) {
    Reader reader(payload, length);
    *message = {};
    message->seq = reader.u32();
    reader.string(message->backend, MESH_BACKEND_ID_MAX_LENGTH);
    message->outgoing = reader.u8() != 0;
    message->delivery = (MeshDeliveryState)reader.u8();
    message->peer.length = reader.u8();
    if (message->peer.length > MESH_ADDRESS_MAX_LENGTH) {
        return false;
    }
    reader.bytes(message->peer.bytes, message->peer.length);
    message->channel = reader.u8();
    reader.string(message->sender_name, MESH_NAME_MAX_LENGTH);
    message->wall_time = (int64_t)reader.u64();
    message->time_source = (MeshTimeSource)reader.u8();
    message->uptime_ms = reader.u64();
    message->rssi = reader.f32();
    message->snr = reader.f32();
    reader.string(message->text, MESH_TEXT_MAX_LENGTH);
    return reader.ok();
}

} // namespace

MeshStore::MeshStore(const Limits& limits) : limits(limits) {}

MeshStore::~MeshStore() {
    close();
}

tt::std::string MeshStore::getSegmentPath(uint32_t segment) const {
    char name[32];
    snprintf(name, sizeof(name), "%s%08u%s", SEGMENT_PREFIX, (unsigned)segment, SEGMENT_SUFFIX);
    return directory + "/" + name;
}

error_t MeshStore::open(const char* path) {
    close();
    const uint64_t startUs = get_micros_since_boot();
    directory = path;
    const error_t made = directory_make(path, true);
    if (made != ERROR_NONE && made != ERROR_ALREADY_EXISTS) {
        LOG_E(TAG, "Failed to create %s", path);
        return ERROR_RESOURCE;
    }

    buffer = allocateBuffer(limits.bufferSize);
    if (buffer == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    bufferUsed = 0;

    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        return ERROR_RESOURCE;
    }
    const size_t prefixLength = strlen(SEGMENT_PREFIX);
    while (dirent* entry = readdir(dir)) {
        if (strncmp(entry->d_name, SEGMENT_PREFIX, prefixLength) == 0 && strstr(entry->d_name, SEGMENT_SUFFIX) != nullptr) {
            segments.push_back((uint32_t)strtoul(entry->d_name + prefixLength, nullptr, 10));
        }
    }
    closedir(dir);
    std::sort(segments.begin(), segments.end());
    const uint64_t listedUs = get_micros_since_boot();

    uint32_t lastValidEnd = 0;
    uint32_t scannedBytes = 0;
    for (uint32_t segment : segments) {
        lastValidEnd = scanSegment(segment);
        scannedBytes += lastValidEnd;
    }

    if (segments.empty()) {
        segments.push_back(0);
        currentSegmentSize = 0;
    } else {
        struct stat info {};
        currentSegmentSize = (stat(getSegmentPath(segments.back()).c_str(), &info) == 0) ? (uint32_t)info.st_size : 0;
        if (currentSegmentSize != lastValidEnd) {
            // Never append after a torn record, as the scan stops there
            segments.push_back(segments.back() + 1);
            currentSegmentSize = 0;
        }
    }

    opened = true;
    const uint64_t endUs = get_micros_since_boot();
    LOG_I(TAG, "Opened %s: %u messages in %u segments, %u KB in %u ms (listing %u ms)", path, (unsigned)index.size(), (unsigned)segments.size(),
        (unsigned)(scannedBytes / 1024), (unsigned)((endUs - startUs) / 1000), (unsigned)((listedUs - startUs) / 1000));
    return ERROR_NONE;
}

void MeshStore::close() {
    if (buffer != nullptr) {
        flush();
        memory_free(buffer);
        buffer = nullptr;
    }
    opened = false;
    index.clear();
    backends.clear();
    segments.clear();
    bufferUsed = 0;
    currentSegmentSize = 0;
    nextSeq = 1;
}

uint32_t MeshStore::scanSegment(uint32_t segment) {
    const tt::std::string path = getSegmentPath(segment);
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        return 0;
    }
    // Every command to an SD card on a shared SPI bus waits for the display and the radio,
    // so the segment is read in one go instead of record by record
    uint8_t* data = (info.st_size > 0) ? allocateBuffer((size_t)info.st_size) : nullptr;
    if (data == nullptr) {
        return scanSegmentStreaming(segment);
    }
    // Not through stdio: one read() lets the filesystem read whole clusters into the buffer at once
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        memory_free(data);
        return scanSegmentStreaming(segment);
    }
    size_t size = 0;
    while (size < (size_t)info.st_size) {
        const ssize_t count = ::read(fd, data + size, (size_t)info.st_size - size);
        if (count <= 0) {
            break;
        }
        size += (size_t)count;
    }
    ::close(fd);
    if (size < (size_t)info.st_size) {
        // A short read is not the end of the segment, so the records are read one by one instead
        LOG_W(TAG, "Short read of segment %u (%u of %u bytes)", (unsigned)segment, (unsigned)size, (unsigned)info.st_size);
        memory_free(data);
        return scanSegmentStreaming(segment);
    }

    uint32_t offset = 0;
    while (offset + RECORD_HEADER_SIZE <= size) {
        const uint8_t* header = data + offset;
        const size_t length = header[4] | (header[5] << 8);
        if (header[0] != RECORD_MAGIC_0 || header[1] != RECORD_MAGIC_1 || length > MESSAGE_PAYLOAD_MAX_SIZE ||
            offset + RECORD_HEADER_SIZE + length > size) {
            // A torn write at the end of the segment
            LOG_W(TAG, "Segment %u is corrupt at offset %u", (unsigned)segment, (unsigned)offset);
            break;
        }
        applyRecord(header[2], header + RECORD_HEADER_SIZE, length, segment, offset);
        offset += RECORD_HEADER_SIZE + length;
    }
    memory_free(data);
    return offset;
}

uint32_t MeshStore::scanSegmentStreaming(uint32_t segment) {
    FILE* file = fopen(getSegmentPath(segment).c_str(), "rb");
    if (file == nullptr) {
        return 0;
    }

    // The records are small, so a larger buffer makes the SD card read in a few big steps instead of many small ones
    setvbuf(file, nullptr, _IOFBF, SCAN_BUFFER_SIZE);

    uint8_t header[RECORD_HEADER_SIZE];
    uint8_t payload[MESSAGE_PAYLOAD_MAX_SIZE];
    uint32_t offset = 0;
    while (fread(header, 1, RECORD_HEADER_SIZE, file) == RECORD_HEADER_SIZE) {
        const size_t length = header[4] | (header[5] << 8);
        if (header[0] != RECORD_MAGIC_0 || header[1] != RECORD_MAGIC_1 || length > sizeof(payload) ||
            fread(payload, 1, length, file) != length) {
            // A torn write at the end of the segment
            LOG_W(TAG, "Segment %u is corrupt at offset %u", (unsigned)segment, (unsigned)offset);
            break;
        }
        applyRecord(header[2], payload, length, segment, offset);
        offset += RECORD_HEADER_SIZE + length;
    }
    fclose(file);
    return offset;
}

uint8_t MeshStore::findBackend(const char* backend) const {
    for (size_t i = 0; i < backends.size(); i++) {
        if (strncmp(backends[i].c_str(), backend, MESH_BACKEND_ID_MAX_LENGTH) == 0) {
            return (uint8_t)i;
        }
    }
    return NO_BACKEND;
}

uint8_t MeshStore::internBackend(const char* backend) {
    const uint8_t existing = findBackend(backend);
    if (existing != NO_BACKEND || backends.size() >= NO_BACKEND) {
        return existing;
    }
    backends.push_back(tt::std::string(backend, strnlen(backend, MESH_BACKEND_ID_MAX_LENGTH)));
    return (uint8_t)(backends.size() - 1);
}

MeshStore::IndexEntry MeshStore::makeIndexEntry(const MeshMessage& message, uint32_t segment, uint32_t offset) {
    return {
        .seq = message.seq,
        .segment = segment,
        .offset = offset,
        .delivery = message.delivery,
        .channel = message.channel,
        .peerHash = hashPeer(message.peer),
        .backend = internBackend(message.backend),
    };
}

uint64_t MeshStore::hashPeerAddress(const MeshAddress* peer) {
    return peer != nullptr ? hashPeer(*peer) : 0;
}

void MeshStore::applyRecord(uint8_t type, const uint8_t* payload, size_t length, uint32_t segment, uint32_t offset) {
    if (type == RECORD_TYPE_MESSAGE) {
        MeshMessage message;
        if (deserializeMessage(payload, length, &message)) {
            index.push_back(makeIndexEntry(message, segment, offset));
            nextSeq = std::max(nextSeq, message.seq + 1);
        }
    } else if (type == RECORD_TYPE_DELETE_CONVERSATION) {
        Reader reader(payload, length);
        char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
        reader.string(backend, MESH_BACKEND_ID_MAX_LENGTH);
        MeshAddress peer {};
        peer.length = reader.u8();
        if (peer.length > MESH_ADDRESS_MAX_LENGTH) {
            return;
        }
        reader.bytes(peer.bytes, peer.length);
        const uint8_t channel = reader.u8();
        const uint32_t lastSeq = reader.u32();
        if (reader.ok()) {
            // Retention can remove all messages that had this sequence number, which still can't be used again
            nextSeq = std::max(nextSeq, lastSeq + 1);
            eraseConversation(backend, peer.length > 0 ? &peer : nullptr, channel, lastSeq);
        }
    } else if (type == RECORD_TYPE_DELIVERY && length >= 5) {
        Reader reader(payload, length);
        const uint32_t seq = reader.u32();
        auto* entry = findEntry(seq);
        if (entry != nullptr) {
            entry->delivery = (MeshDeliveryState)reader.u8();
        }
    }
}

void MeshStore::eraseConversation(const char* backend, const MeshAddress* peer, uint8_t channel, uint32_t lastSeq) {
    const uint64_t peerHash = hashPeerAddress(peer);
    const uint8_t backendId = findBackend(backend);
    if (backendId == NO_BACKEND) {
        return;
    }
    std::erase_if(index, [&](const IndexEntry& entry) {
        return entry.seq <= lastSeq && entry.backend == backendId && entry.peerHash == peerHash && (peer != nullptr || entry.channel == channel);
    });
}

error_t MeshStore::removeConversation(const char* backend, const MeshAddress* peer, uint8_t channel) {
    // A zero-length address means a channel conversation
    if (peer != nullptr && peer->length == 0) {
        peer = nullptr;
    }
    const uint32_t lastSeq = nextSeq - 1;
    uint8_t payload[1 + MESH_BACKEND_ID_MAX_LENGTH + 1 + MESH_ADDRESS_MAX_LENGTH + 1 + 4];
    Writer writer(payload, sizeof(payload));
    writer.string(backend, MESH_BACKEND_ID_MAX_LENGTH);
    writer.u8(peer != nullptr ? peer->length : 0);
    if (peer != nullptr) {
        writer.bytes(peer->bytes, std::min<size_t>(peer->length, MESH_ADDRESS_MAX_LENGTH));
    }
    writer.u8(channel);
    writer.u32(lastSeq);
    // The record comes first: deleting from the index without it would bring the messages back at the next open
    const error_t result = appendRecord(RECORD_TYPE_DELETE_CONVERSATION, payload, writer.size(), 0);
    if (result != ERROR_NONE) {
        return result;
    }
    eraseConversation(backend, peer, channel, lastSeq);
    return flush();
}

error_t MeshStore::appendRecord(uint8_t type, const uint8_t* payload, size_t length, uint32_t seq) {
    if (buffer == nullptr) {
        return ERROR_INVALID_STATE;
    }
    const size_t recordSize = RECORD_HEADER_SIZE + length;
    if (bufferUsed + recordSize > limits.bufferSize) {
        const error_t result = flush();
        if (result != ERROR_NONE) {
            return result;
        }
    }

    uint8_t* record = buffer + bufferUsed;
    record[0] = RECORD_MAGIC_0;
    record[1] = RECORD_MAGIC_1;
    record[2] = type;
    record[3] = 0;
    record[4] = (uint8_t)length;
    record[5] = (uint8_t)(length >> 8);
    memcpy(record + RECORD_HEADER_SIZE, payload, length);

    if (type == RECORD_TYPE_MESSAGE) {
        MeshMessage message;
        deserializeMessage(payload, length, &message);
        index.push_back(makeIndexEntry(message, PENDING, (uint32_t)bufferUsed));
    }

    if (bufferUsed == 0) {
        firstPendingMs = 0;
    }
    bufferUsed += recordSize;
    return ERROR_NONE;
}

error_t MeshStore::add(MeshMessage* message) {
    message->seq = nextSeq++;
    uint8_t payload[MESSAGE_PAYLOAD_MAX_SIZE];
    const size_t length = serializeMessage(*message, payload, sizeof(payload));
    if (length == 0) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return appendRecord(RECORD_TYPE_MESSAGE, payload, length, message->seq);
}

error_t MeshStore::setDelivery(uint32_t seq, enum MeshDeliveryState delivery) {
    auto* entry = findEntry(seq);
    if (entry == nullptr) {
        return ERROR_NOT_FOUND;
    }
    entry->delivery = delivery;
    uint8_t payload[5];
    Writer writer(payload, sizeof(payload));
    writer.u32(seq);
    writer.u8((uint8_t)delivery);
    return appendRecord(RECORD_TYPE_DELIVERY, payload, sizeof(payload), seq);
}

MeshStore::IndexEntry* MeshStore::findEntry(uint32_t seq) {
    auto iterator = std::lower_bound(index.begin(), index.end(), seq, [](const IndexEntry& entry, uint32_t value) {
        return entry.seq < value;
    });
    return (iterator != index.end() && iterator->seq == seq) ? &*iterator : nullptr;
}

const MeshStore::IndexEntry* MeshStore::findEntry(uint32_t seq) const {
    return const_cast<MeshStore*>(this)->findEntry(seq);
}

error_t MeshStore::readEntry(const IndexEntry& entry, MeshMessage* message) const {
    uint8_t header[RECORD_HEADER_SIZE];
    uint8_t payload[MESSAGE_PAYLOAD_MAX_SIZE];
    size_t length;

    if (entry.segment == PENDING) {
        memcpy(header, buffer + entry.offset, RECORD_HEADER_SIZE);
        length = header[4] | (header[5] << 8);
        memcpy(payload, buffer + entry.offset + RECORD_HEADER_SIZE, length);
    } else {
        // Not through stdio: its buffer would read far more of the segment than the one record.
        // One read covers the header and the largest payload, as the length is only known from the header.
        uint8_t record[RECORD_HEADER_SIZE + MESSAGE_PAYLOAD_MAX_SIZE];
        const int fd = ::open(getSegmentPath(entry.segment).c_str(), O_RDONLY);
        if (fd < 0) {
            return ERROR_RESOURCE;
        }
        const ssize_t count = (::lseek(fd, entry.offset, SEEK_SET) == (off_t)entry.offset) ? ::read(fd, record, sizeof(record)) : -1;
        ::close(fd);
        if (count < (ssize_t)RECORD_HEADER_SIZE) {
            return ERROR_RESOURCE;
        }
        memcpy(header, record, RECORD_HEADER_SIZE);
        length = header[4] | (header[5] << 8);
        if (length > sizeof(payload) || RECORD_HEADER_SIZE + length > (size_t)count) {
            return ERROR_RESOURCE;
        }
        memcpy(payload, record + RECORD_HEADER_SIZE, length);
    }

    if (!deserializeMessage(payload, length, message)) {
        return ERROR_RESOURCE;
    }
    message->delivery = entry.delivery;
    return ERROR_NONE;
}

error_t MeshStore::get(uint32_t seq, MeshMessage* message) const {
    const auto* entry = findEntry(seq);
    if (entry == nullptr) {
        return ERROR_NOT_FOUND;
    }
    return readEntry(*entry, message);
}

size_t MeshStore::getConversation(const char* backend, const MeshAddress* peer, uint8_t channel, uint32_t beforeSeq, MeshMessage* messages, size_t maxCount) const {
    const uint64_t peerHash = hashPeerAddress(peer);
    size_t count = 0;
    for (auto iterator = index.rbegin(); iterator != index.rend() && count < maxCount; ++iterator) {
        if (beforeSeq != 0 && iterator->seq >= beforeSeq) {
            continue;
        }
        if (iterator->peerHash != peerHash || (peer == nullptr && iterator->channel != channel)) {
            continue;
        }
        MeshMessage* message = &messages[count];
        // The hash can collide, so the stored message decides
        if (readEntry(*iterator, message) != ERROR_NONE || strncmp(message->backend, backend, MESH_BACKEND_ID_MAX_LENGTH) != 0) {
            continue;
        }
        if (peer != nullptr ? !isSameAddress(message->peer, *peer) : message->peer.length != 0) {
            continue;
        }
        count++;
    }
    return count;
}

size_t MeshStore::getAfter(uint32_t afterSeq, MeshMessage* messages, size_t maxCount) const {
    size_t count = 0;
    for (auto iterator = index.begin(); iterator != index.end() && count < maxCount; ++iterator) {
        if (iterator->seq > afterSeq && readEntry(*iterator, &messages[count]) == ERROR_NONE) {
            count++;
        }
    }
    return count;
}

size_t MeshStore::getHistory(uint32_t beforeSeq, MeshMessage* messages, size_t maxCount) const {
    size_t count = 0;
    for (auto iterator = index.rbegin(); iterator != index.rend() && count < maxCount; ++iterator) {
        if (beforeSeq != 0 && iterator->seq >= beforeSeq) {
            continue;
        }
        if (readEntry(*iterator, &messages[count]) == ERROR_NONE) {
            count++;
        }
    }
    return count;
}

tt::std::vector<uint32_t> MeshStore::findOutgoing(const char* backend, enum MeshDeliveryState delivery) const {
    tt::std::vector<uint32_t> result;
    MeshMessage message;
    for (const auto& entry : index) {
        if (entry.delivery == delivery && readEntry(entry, &message) == ERROR_NONE && message.outgoing &&
            strncmp(message.backend, backend, MESH_BACKEND_ID_MAX_LENGTH) == 0) {
            result.push_back(entry.seq);
        }
    }
    return result;
}

error_t MeshStore::flush() {
    if (buffer == nullptr || bufferUsed == 0) {
        return ERROR_NONE;
    }

    if (currentSegmentSize >= limits.segmentSize) {
        segments.push_back(segments.back() + 1);
        currentSegmentSize = 0;
    }

    const uint32_t segment = segments.back();
    FILE* file = fopen(getSegmentPath(segment).c_str(), "ab");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open segment %u", (unsigned)segment);
        return ERROR_RESOURCE;
    }
    // A missing segment is recreated empty, so the pending offsets start at the real end of the file
    if (fseek(file, 0, SEEK_END) == 0) {
        const long end = ftell(file);
        if (end >= 0) {
            currentSegmentSize = (uint32_t)end;
        }
    }
    const size_t written = fwrite(buffer, 1, bufferUsed, file);
    fclose(file);
    if (written != bufferUsed) {
        LOG_E(TAG, "Failed to write segment %u", (unsigned)segment);
        // The segment may end with a partial record now, so the retry goes to a new one
        currentSegmentSize = (uint32_t)limits.segmentSize;
        return ERROR_RESOURCE;
    }

    for (auto& entry : index) {
        if (entry.segment == PENDING) {
            entry.segment = segment;
            entry.offset += currentSegmentSize;
        }
    }
    currentSegmentSize += (uint32_t)bufferUsed;
    bufferUsed = 0;
    // Old segments are only deleted once the newer records are safely written
    enforceBudget();
    return ERROR_NONE;
}

void MeshStore::flushIfNeeded(uint64_t nowMs, uint64_t maxAgeMs) {
    if (bufferUsed == 0) {
        return;
    }
    if (firstPendingMs == 0) {
        firstPendingMs = nowMs;
    }
    if (bufferUsed * 4 >= limits.bufferSize * 3 || (nowMs - firstPendingMs) >= maxAgeMs) {
        flush();
    }
}

void MeshStore::enforceBudget() {
    while (segments.size() > limits.segmentCount) {
        const uint32_t oldest = segments.front();
        segments.erase(segments.begin());
        remove(getSegmentPath(oldest).c_str());
        std::erase_if(index, [oldest](const IndexEntry& entry) { return entry.segment == oldest; });
        LOG_I(TAG, "Deleted segment %u", (unsigned)oldest);
    }
}
