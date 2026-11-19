// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <mesh/mesh.h>

#define MESH_SERVICE_ID "tactility.mesh"

struct MeshSettings {
    bool enabled;
    char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
    char node_name[MESH_NAME_MAX_LENGTH + 1];
    /** The name of the radio device, empty for the default radio */
    char radio[MESH_NAME_MAX_LENGTH + 1];
    /** For radios that support it, which is the SX126x family */
    bool rx_boosted_gain;
};

/** The settings of one backend, which are kept for backends that aren't selected */
struct MeshBackendSettings {
    /** Empty for the backend's default preset */
    char preset[MESH_NAME_MAX_LENGTH + 1];
    struct MeshRadioSettings radio_settings;
    struct MeshNetworkSettings network;
};

/**
 * Loads the settings of a backend, or defaults when there are none.
 * @retval ERROR_RESOURCE when the settings file couldn't be opened, in which case @a settings has the defaults and the load can be tried again
 */
error_t mesh_settings_load_backend(const char* backend, struct MeshBackendSettings* settings);

/**
 * Settings were once stored without a backend, for the backend that was selected. Moves them to @a owner once.
 * Settings of a backend are never read from the unqualified keys, so no other backend can take them.
 * @retval ERROR_NONE when the settings were moved, or were moved before
 * @retval ERROR_RESOURCE when the file couldn't be opened or written, so it can be tried again
 */
error_t mesh_settings_migrate_legacy(const char* owner);

error_t mesh_settings_save_backend(const char* backend, const struct MeshBackendSettings* settings);

/** Visits the conversations that have notifications turned off, as the tokens that mesh_settings_save_muted() was given */
void mesh_settings_load_muted(void (*visitor)(const char* token, void* context), void* context);

/**
 * Stores whether a conversation has notifications turned off.
 * @param[in] token identifies the conversation, e.g. "meshcore.c1" for a channel or "meshcore.p<hex>" for a contact
 */
error_t mesh_settings_save_muted(const char* token, bool muted);

/**
 * Visits the stored number of unread messages of each conversation. The key is the token that mesh_settings_save_unread()
 * was given, and the value is the count.
 */
void mesh_settings_load_unread(void (*visitor)(const char* token, uint32_t count, void* context), void* context);

/** Stores the number of unread messages of conversations in one update, under tokens that mesh_settings_load_unread() visits */
error_t mesh_settings_save_unread(const char* const* tokens, const uint32_t* counts, size_t length);

/** Loads the settings, or defaults when there are none */
void mesh_settings_load(struct MeshSettings* settings);

error_t mesh_settings_save(const struct MeshSettings* settings);
