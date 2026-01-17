#include "tlora_pager.h"
#include <esp_log.h>
#include <stdio.h>

#define TAG "tlora_pager"

const struct tlora_pager_api tlora_pager_api_instance = {
};

int tlora_pager_init(const struct device* device) {
    ESP_LOGI(TAG, "init %s", device->name);
    return 0;
}

int tlora_pager_deinit(const struct device* device) {
    ESP_LOGI(TAG, "deinit %s", device->name);
    return 0;
}
