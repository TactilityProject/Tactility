// SPDX-License-Identifier: Apache-2.0
#include <tactility/error.h>

extern "C" {

const char* error_to_string(error_t error) {
    switch (error) {
        case ERROR_NONE:
            return "no error";
        case ERROR_UNDEFINED:
            return "undefined";
        case ERROR_INVALID_STATE:
            return "invalid state";
        case ERROR_INVALID_ARGUMENT:
            return "invalid argument";
        case ERROR_MISSING_PARAMETER:
            return "missing parameter";
        case ERROR_NOT_FOUND:
            return "not found";
        case ERROR_ISR_STATUS:
            return "ISR status";
        case ERROR_RESOURCE:
            return "resource";
        case ERROR_TIMEOUT:
            return "timeout";
        case ERROR_OUT_OF_MEMORY:
            return "out of memory";
        case ERROR_NOT_SUPPORTED:
            return "not supported";
        case ERROR_NOT_ALLOWED:
            return "not allowed";
        case ERROR_BUFFER_OVERFLOW:
            return "buffer overflow";
        default:
            return "unknown";
    }
}

}
