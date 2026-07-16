// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/esp_hosted_ota.h>
#include <stddef.h>

static const struct EspHostedOtaApi* registeredApi = NULL;

void esp_hosted_ota_register(const struct EspHostedOtaApi* api) {
    registeredApi = api;
}

bool esp_hosted_ota_wait_for_transport(uint32_t timeoutMs) {
    if (registeredApi == NULL) {
        return false;
    }
    return registeredApi->wait_for_transport(timeoutMs);
}

error_t esp_hosted_ota_get_coprocessor_fwversion(uint32_t* major, uint32_t* minor, uint32_t* patch) {
    if (registeredApi == NULL) {
        return ERROR_NOT_SUPPORTED;
    }
    return registeredApi->get_coprocessor_fwversion(major, minor, patch);
}

error_t esp_hosted_ota_get_cp_info(uint32_t* chip_id, char* target_name, size_t target_name_len) {
    if (registeredApi == NULL) {
        return ERROR_NOT_SUPPORTED;
    }
    return registeredApi->get_cp_info(chip_id, target_name, target_name_len);
}

error_t esp_hosted_ota_begin(void) {
    if (registeredApi == NULL) {
        return ERROR_NOT_SUPPORTED;
    }
    return registeredApi->begin();
}

error_t esp_hosted_ota_write(const uint8_t* data, size_t length) {
    if (registeredApi == NULL) {
        return ERROR_NOT_SUPPORTED;
    }
    return registeredApi->write(data, length);
}

error_t esp_hosted_ota_end(void) {
    if (registeredApi == NULL) {
        return ERROR_NOT_SUPPORTED;
    }
    return registeredApi->end();
}

error_t esp_hosted_ota_activate(void) {
    if (registeredApi == NULL) {
        return ERROR_NOT_SUPPORTED;
    }
    return registeredApi->activate();
}
