// SPDX-License-Identifier: Apache-2.0
#include <Tactility/ErrorEsp32.h>

error_t esp_err_to_error(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return ERROR_NONE;
        case ESP_ERR_INVALID_ARG:
            return ERROR_INVALID_ARGUMENT;
        case ESP_ERR_INVALID_STATE:
            return ERROR_INVALID_STATE;
        case ESP_ERR_TIMEOUT:
            return ERROR_TIMEOUT;
        default:
            return ERROR_UNDEFINED;
    }
}
