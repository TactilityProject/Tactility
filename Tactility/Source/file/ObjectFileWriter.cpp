#include <Tactility/file/ObjectFile.h>
#include <Tactility/file/ObjectFilePrivate.h>
#include <Tactility/Logger.h>

#include <cstring>
#include <unistd.h>

namespace tt::file {

static const auto LOGGER = Logger("ObjectFileWriter");

bool ObjectFileWriter::open() {
    bool edit_existing = append && access(filePath.c_str(), F_OK) == 0;
    if (append && !edit_existing) {
        LOGGER.warn("access() to {} failed: {}", filePath, strerror(errno));
    }

    // Edit existing or create a new file
    auto opening_file = std::unique_ptr<FILE, FileCloser>(std::fopen(filePath.c_str(), edit_existing ? "rb+" : "wb"));
    if (opening_file == nullptr) {
        LOGGER.error("Failed to open file {}", filePath);
        return false;
    }

    auto file_size = getSize(opening_file.get());
    if (file_size > 0 && edit_existing) {

        // Read and parse file header

        FileHeader file_header;
        if (fread(&file_header, sizeof(FileHeader), 1, opening_file.get()) != 1) {
            LOGGER.error("Failed to read file header from {}", filePath);
            return false;
        }

        if (file_header.identifier != OBJECT_FILE_IDENTIFIER) {
            LOGGER.error("Invalid file type for {}", filePath);
            return false;
        }

        if (file_header.version != OBJECT_FILE_VERSION) {
            LOGGER.error("Unknown version for {}: {}", filePath, file_header.version);
            return false;
        }

        // Read and parse content header

        ContentHeader content_header;
        if (fread(&content_header, sizeof(ContentHeader), 1, opening_file.get()) != 1) {
            LOGGER.error("Failed to read content header from {}", filePath);
            return false;
        }

        if (recordSize != content_header.recordSize) {
            LOGGER.error("Record size mismatch for {}: expected {}, got {}", filePath, recordSize, content_header.recordSize);
            return false;
        }

        if (recordVersion != content_header.recordVersion) {
            LOGGER.error("Version mismatch for {}: expected {}, got {}", filePath, recordVersion, content_header.recordVersion);
            return false;
        }

        recordsWritten = content_header.recordCount;
        fseek(opening_file.get(), 0, SEEK_END);
    } else {
        FileHeader file_header;
        if (fwrite(&file_header, sizeof(FileHeader), 1, opening_file.get()) != 1) {
            LOGGER.error("Failed to write file header for {}", filePath);
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
        LOGGER.error("File not opened: {}", filePath);
        return;
    }

    if (fseek(file.get(), sizeof(FileHeader), SEEK_SET) != 0) {
        LOGGER.error("File seek failed: {}", filePath);
        return;
    }

    ContentHeader content_header = {
        .recordVersion = this->recordVersion,
        .recordSize = this->recordSize,
        .recordCount = this->recordsWritten
    };

    if (fwrite(&content_header, sizeof(ContentHeader), 1, file.get()) != 1) {
        LOGGER.error("Failed to write content header to {}", filePath);
    }

    file = nullptr;
}

bool ObjectFileWriter::write(void* data) {
    if (file == nullptr) {
        LOGGER.error("File not opened: {}", filePath);
        return false;
    }

    if (fwrite(data, recordSize, 1, file.get()) != 1) {
        LOGGER.error("Failed to write record to {}", filePath);
        return false;
    }

    recordsWritten++;

    return true;
}

// endregion Writer

}