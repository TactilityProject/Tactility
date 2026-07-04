#include <Tactility/file/ObjectFile.h>
#include <Tactility/file/ObjectFilePrivate.h>
#include <tactility/log.h>

#include <cstring>
#include <unistd.h>

namespace tt::file {

constexpr auto* TAG = "ObjectFileWriter";

bool ObjectFileWriter::open() {
    bool edit_existing = append && access(filePath.c_str(), F_OK) == 0;
    if (append && !edit_existing) {
        LOG_W(TAG, "access() to %s failed: %s", filePath.c_str(), strerror(errno));
    }

    // Edit existing or create a new file
    auto opening_file = std::unique_ptr<FILE, FileCloser>(std::fopen(filePath.c_str(), edit_existing ? "rb+" : "wb"));
    if (opening_file == nullptr) {
        LOG_E(TAG, "Failed to open file %s", filePath.c_str());
        return false;
    }

    auto file_size = getSize(opening_file.get());
    if (file_size > 0 && edit_existing) {

        // Read and parse file header

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
            LOG_E(TAG, "Unknown version for %s: %u", filePath.c_str(), (unsigned)file_header.version);
            return false;
        }

        // Read and parse content header

        ContentHeader content_header;
        if (fread(&content_header, sizeof(ContentHeader), 1, opening_file.get()) != 1) {
            LOG_E(TAG, "Failed to read content header from %s", filePath.c_str());
            return false;
        }

        if (recordSize != content_header.recordSize) {
            LOG_E(TAG, "Record size mismatch for %s: expected %u, got %u", filePath.c_str(), (unsigned)recordSize, (unsigned)content_header.recordSize);
            return false;
        }

        if (recordVersion != content_header.recordVersion) {
            LOG_E(TAG, "Version mismatch for %s: expected %u, got %u", filePath.c_str(), (unsigned)recordVersion, (unsigned)content_header.recordVersion);
            return false;
        }

        recordsWritten = content_header.recordCount;
        fseek(opening_file.get(), 0, SEEK_END);
    } else {
        FileHeader file_header;
        if (fwrite(&file_header, sizeof(FileHeader), 1, opening_file.get()) != 1) {
            LOG_E(TAG, "Failed to write file header for %s", filePath.c_str());
            return false;
        }

        // Seek forward (skip ContentHeader that will be written later)
        fseek(opening_file.get(), sizeof(ContentHeader), SEEK_CUR);
    }


    file = std::move(opening_file);
    return true;
}

void ObjectFileWriter::close() {
    if (file == nullptr) {
        LOG_E(TAG, "File not opened: %s", filePath.c_str());
        return;
    }

    if (fseek(file.get(), sizeof(FileHeader), SEEK_SET) != 0) {
        LOG_E(TAG, "File seek failed: %s", filePath.c_str());
        return;
    }

    ContentHeader content_header = {
        .recordVersion = this->recordVersion,
        .recordSize = this->recordSize,
        .recordCount = this->recordsWritten
    };

    if (fwrite(&content_header, sizeof(ContentHeader), 1, file.get()) != 1) {
        LOG_E(TAG, "Failed to write content header to %s", filePath.c_str());
    }

    file = nullptr;
}

bool ObjectFileWriter::write(void* data) {
    if (file == nullptr) {
        LOG_E(TAG, "File not opened: %s", filePath.c_str());
        return false;
    }

    if (fwrite(data, recordSize, 1, file.get()) != 1) {
        LOG_E(TAG, "Failed to write record to %s", filePath.c_str());
        return false;
    }

    recordsWritten++;

    return true;
}

// endregion Writer

}