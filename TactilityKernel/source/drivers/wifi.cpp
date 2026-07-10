// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/wifi.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define WIFI_API(device) ((const struct WifiApi*)device_get_driver(device)->api)

extern "C" {

struct Device* wifi_find_first_registered_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&WIFI_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        *static_cast<struct Device**>(ctx) = dev;
        return false;
    });
    return found;
}

error_t wifi_get_radio_state(struct Device* device, enum WifiRadioState* state) {
    return WIFI_API(device)->get_radio_state(device, state);
}

error_t wifi_get_station_state(struct Device* device, enum WifiStationState* state) {
    return WIFI_API(device)->get_station_state(device, state);
}

error_t wifi_get_access_point_state(struct Device* device, enum WifiAccessPointState* state) {
    return WIFI_API(device)->get_access_point_state(device, state);
}

bool wifi_is_scanning(struct Device* device) {
    return WIFI_API(device)->is_scanning(device);
}

error_t wifi_scan(struct Device* device) {
    return WIFI_API(device)->scan(device);
}

error_t wifi_get_scan_results(struct Device* device, struct WifiApRecord* results, size_t* num_results) {
    return WIFI_API(device)->get_scan_results(device, results, num_results);
}

error_t wifi_station_get_ipv4_address(struct Device* device, char* ipv4) {
    return WIFI_API(device)->station_get_ipv4_address(device, ipv4);
}

error_t wifi_station_get_target_ssid(struct Device* device, char* ssid) {
    return WIFI_API(device)->station_get_target_ssid(device, ssid);
}

error_t wifi_station_connect(struct Device* device, const char* ssid, const char* password, int32_t channel) {
    return WIFI_API(device)->station_connect(device, ssid, password, channel);
}

error_t wifi_station_disconnect(struct Device* device) {
    return WIFI_API(device)->station_disconnect(device);
}

error_t wifi_station_get_rssi(struct Device* device, int32_t* rssi) {
    return WIFI_API(device)->station_get_rssi(device, rssi);
}

error_t wifi_add_event_callback(struct Device* device, void* callback_context, WifiEventCallback callback) {
    return WIFI_API(device)->add_event_callback(device, callback_context, callback);
}

error_t wifi_remove_event_callback(struct Device* device, WifiEventCallback callback) {
    return WIFI_API(device)->remove_event_callback(device, callback);
}

const struct DeviceType WIFI_TYPE = {
    .name = "wifi"
};

} // extern "C"
