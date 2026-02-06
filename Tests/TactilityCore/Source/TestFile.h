#pragma once

#include "doctest.h"
#include <unistd.h>

#include <Tactility/file/File.h>

/**
 * A class for creating test files that can automatically clean themselves up.
 */
class TestFile {
    const char* path;
    bool autoClean;

public:

    TestFile(const char* path, bool autoClean = true) : path(path), autoClean(autoClean) {
        if (autoClean && exists()) {
            remove();
        }
    }

    ~TestFile() {
        if (autoClean && exists()) {
            remove();
        }
    }

    const char* getPath() const { return path; }

    void writeData(const char* data) const {
        CHECK_EQ(tt::file::writeString(path, data), true);
    }

    bool exists() const {
        return access(path, F_OK) == 0;
    }

    void remove() const {
        ::remove(path);
    }
};