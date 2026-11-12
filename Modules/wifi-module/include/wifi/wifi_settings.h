// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 32 characters/octets, according to IEEE 802.11-2020 spec */
#define WIFI_SETTINGS_SSID_LIMIT 32
/** 64 characters/octets, according to IEEE 802.11-2020 spec */
#define WIFI_SETTINGS_PASSWORD_LIMIT 64
/** Default auto-connect setting for new access points */
#define WIFI_SETTINGS_AUTO_CONNECT_DEFAULT true

/** The persisted settings of an access point. */
struct WifiApSettings {
    char ssid[WIFI_SETTINGS_SSID_LIMIT + 1];
    char password[WIFI_SETTINGS_PASSWORD_LIMIT + 1];
    bool auto_connect;
    /** The Wi-Fi channel, or 0 for any */
    int32_t channel;
};

/**
 * @param[in] ssid the access point to look for
 * @return true if settings exist for the provided SSID
 */
bool wifi_settings_contains(const char* ssid);

/**
 * Load the settings for the provided SSID.
 * @param[in] ssid the access point to look for
 * @param[out] settings the loaded settings
 * @retval ERROR_NOT_FOUND no settings exist for the SSID
 * @retval ERROR_RESOURCE the settings could not be read or decrypted
 * @retval ERROR_NONE on success
 */
error_t wifi_settings_load(const char* ssid, struct WifiApSettings* settings);

/**
 * Save the settings of an access point. The password is stored encrypted.
 * @param[in] settings the settings to save
 * @retval ERROR_INVALID_ARGUMENT the SSID is empty
 * @retval ERROR_RESOURCE the settings could not be written
 * @retval ERROR_NONE on success
 */
error_t wifi_settings_save(const struct WifiApSettings* settings);

/**
 * Change only the auto-connect setting of saved settings. Other wifi_settings_* calls can't
 * interleave between the read and the write.
 * @param[in] ssid the access point to change the settings for
 * @param[in] auto_connect the new auto-connect value
 * @retval ERROR_NOT_FOUND no settings exist for the SSID
 * @retval ERROR_RESOURCE the settings could not be read or written
 * @retval ERROR_NONE on success
 */
error_t wifi_settings_set_auto_connect(const char* ssid, bool auto_connect);

/**
 * Remove settings that were saved previously.
 * @param[in] ssid the access point to remove the settings for
 * @retval ERROR_NOT_FOUND no settings exist for the SSID
 * @retval ERROR_RESOURCE the settings could not be removed
 * @retval ERROR_NONE on success
 */
error_t wifi_settings_remove(const char* ssid);

/**
 * Iterate over the SSIDs of all saved access points.
 * @param[in] context passed through to on_ssid, can be NULL
 * @param[in] on_ssid called once per saved access point, return false to stop iterating
 */
void wifi_settings_for_each(void* context, bool (*on_ssid)(const char* ssid, void* context));

/**
 * Persist whether Wi-Fi is on. The radio is turned on when booting while it's enabled.
 * @param[in] enable whether Wi-Fi is on
 * @retval ERROR_RESOURCE the setting could not be written
 * @retval ERROR_NONE on success
 */
error_t wifi_settings_set_enable(bool enable);

/** @return true when Wi-Fi is on, see wifi_settings_set_enable() */
bool wifi_settings_get_enable(void);

#ifdef __cplusplus
}
#endif
