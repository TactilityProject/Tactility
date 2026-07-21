#include <Tactility/file/ObjectFile.h>
#include <Tactility/file/ObjectFilePrivate.h>

#include <cstring>
#include <tactility/log.h>

namespace tt::file {

constexpr auto* TAG = "ObjectFileReader";

bool ObjectFileReader::open() {
    auto opening_file = std::unique_ptr<FILE, FileCloser>(fopen(filePath.c_str(), "r"));
    if (opening_file == nullptr) {
        LOG_E(TAG, "Failed to open file %s", filePath.c_str());
        return false;
    }

    FileHeader file_header;
    if (fread(&file_header, sizeof(FileHeader), 1, opening_file.get()) != 1) {
        LOG_E(TAG, "Failed to read file header from %s", filePath.c_str());
        return false;
    }

    if (file_header.identifier != OBJECT_FILE_IDENTIFIER) {
        LOG_E(TAG, "Invalid file type for %s", filePath.c_str());
        return false;
    }

    if (file_header.version != OBJECT_FILE_VERSION) {
        LOG_E(TAG, "Unknown version for %s: %u", filePath.c_str(), (unsigned)file_header.identifier);
        return false;
    }

    ContentHeader content_header;
    if (fread(&content_header, sizeof(ContentHeader), 1, opening_file.get()) != 1) {
        LOG_E(TAG, "Failed to read content header from %s", filePath.c_str());
        return false;
    }

    if (recordSize != content_header.recordSize) {
        LOG_E(TAG, "Record size mismatch for %s: expected %u, got %u", filePath.c_str(), (unsigned)recordSize, (unsigned)content_header.recordSize);
        return false;
    }

    recordCount = content_header.recordCount;
    recordVersion = content_header.recordVersion;

    file = std::move(opening_file);

    LOG_D(TAG, "File version: %u", (unsigned)file_header.version);
    LOG_D(TAG, "Content: version = %u, size = %u bytes, count = %u", (unsigned)content_header.recordVersion, (unsigned)content_header.recordSize, (unsigned)content_header.recordCount);

    return true;
}

void ObjectFileReader::close() {
    recordCount = 0;
    recordVersion = 0;
    recordsRead = 0;

    file = nullptr;
}

bool ObjectFileReader::readNext(void* output) {
    if (file == nullptr) {
        LOG_E(TAG, "File not open");
        return false;
    }

    bool result = fread(output, recordSize, 1, file.get()) == 1;
    if (result) {
        recordsRead++;
    }

    return result;
}

}