// SPDX-License-Identifier: Apache-2.0
#include <tactility/network.h>

#include <tactility/device.h>
#include <tactility/drivers/wifi.h>

static bool check_wifi_connected(struct Device* device, void* context) {
    if (!device_is_ready(device)) {
        return true;
    }
    enum WifiStationState state = WIFI_STATION_STATE_DISCONNECTED;
    if (wifi_get_station_state(device, &state) == ERROR_NONE && state == WIFI_STATION_STATE_CONNECTED) {
        *(bool*)context = true;
        return false;
    }
    return true;
}

bool network_is_available(void) {
    bool available = false;
    device_for_each_of_type(&WIFI_TYPE, &available, check_wifi_connected);
    return available;
}
