#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/file_ops.h>

#include <esp_err.h>
#include <nvs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

/**
 * Per-identity BLE presence: advertising address and bond store.
 *
 * A controller has one public address, so apps that only change the device name all look like
 * the same peer to a PC and share its single bond. Two things make each identity a separate
 * device:
 *  - a static random advertising address derived from the controller's public address and the
 *    identity hash (ble_identity_derive_addr()),
 *  - a separate bond set, because NimBLE keys store records by peer address only.
 *
 * Bond sets are swapped wholesale between IDF's "nimble_bond" namespace and one
 * "btid_<hash>" archive namespace per identity.
 */

constexpr auto* TAG = "esp32_ble_id";

namespace {

/** Namespace owned by IDF's NimBLE store (see components/bt/.../ble_store_nvs.c). */
constexpr const char* BOND_NAMESPACE = "nimble_bond";

/** Namespace holding the record of which identity currently owns BOND_NAMESPACE. */
constexpr const char* META_NAMESPACE = "btid_meta";
constexpr const char* META_KEY_OWNER  = "owner";

/** Owner marker written while a bond-set switch is in progress (never a valid identity hash). */
constexpr uint32_t OWNER_TRANSITION = 0xFFFFFFFFu;

/** "btid_" + 8 hex digits + NUL. Fits NVS_NS_NAME_MAX_SIZE (16). */
constexpr size_t SLOT_NAMESPACE_SIZE = 16;

void get_slot_namespace(uint32_t identity_hash, char out[SLOT_NAMESPACE_SIZE]) {
    snprintf(out, SLOT_NAMESPACE_SIZE, "btid_%08lx", (unsigned long)identity_hash);
}

/** @return true when the owner marker was read; false when it is absent or unreadable. */
bool read_owner(uint32_t* owner) {
    nvs_handle_t handle;
    if (nvs_open(META_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t length = sizeof(*owner);
    esp_err_t err = nvs_get_blob(handle, META_KEY_OWNER, owner, &length);
    nvs_close(handle);
    return err == ESP_OK && length == sizeof(*owner);
}

bool write_owner(uint32_t owner) {
    nvs_handle_t handle;
    if (nvs_open(META_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, META_KEY_OWNER, &owner, sizeof(owner));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

/** Erase every key in a namespace. A namespace that does not exist yet counts as success. */
bool clear_namespace(const char* name) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

/**
 * Replace the contents of @a dst_name with a copy of every blob in @a src_name.
 * @return false when @a src_name does not exist or a read or write failed. @a dst_name is then
 *         cleared, which is the desired state for an identity that never paired.
 */
bool copy_namespace(const char* src_name, const char* dst_name) {
    nvs_handle_t src;
    if (nvs_open(src_name, NVS_READONLY, &src) != ESP_OK) {
        return false;
    }

    // Collect key names before writing. A write can trigger NVS compaction, which must not run
    // while an iterator is live. A bond set holds a handful of records, 32 keys is ample.
    constexpr size_t MAX_KEYS = 32;
    char keys[MAX_KEYS][NVS_KEY_NAME_MAX_SIZE] = {};
    size_t key_count = 0;

    nvs_iterator_t iterator = nullptr;
    esp_err_t err = nvs_entry_find_in_handle(src, NVS_TYPE_BLOB, &iterator);
    while (err == ESP_OK && key_count < MAX_KEYS) {
        nvs_entry_info_t info;
        nvs_entry_info(iterator, &info);
        strncpy(keys[key_count], info.key, NVS_KEY_NAME_MAX_SIZE - 1);
        ++key_count;
        err = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    // A successful iteration ends with ESP_ERR_NVS_NOT_FOUND. Hitting the cap with more keys
    // left would archive an incomplete set, so refuse instead.
    if (key_count == MAX_KEYS && err == ESP_OK) {
        LOG_E(TAG, "bond namespace holds more than %u keys, refusing to archive", (unsigned)MAX_KEYS);
        nvs_close(src);
        return false;
    }
    bool ok = (err == ESP_ERR_NVS_NOT_FOUND);

    nvs_handle_t dst;
    if (nvs_open(dst_name, NVS_READWRITE, &dst) != ESP_OK) {
        nvs_close(src);
        return false;
    }
    if (ok) {
        ok = nvs_erase_all(dst) == ESP_OK;
    }

    size_t capacity = 256;
    auto* buffer = static_cast<uint8_t*>(malloc(capacity));
    if (ok && buffer == nullptr) {
        ok = false;
    }

    for (size_t i = 0; ok && i < key_count; ++i) {
        size_t length = 0;
        if (nvs_get_blob(src, keys[i], nullptr, &length) != ESP_OK || length == 0) {
            // Entry vanished or is not a blob; nothing to copy.
            continue;
        }
        if (length > capacity) {
            auto* grown = static_cast<uint8_t*>(realloc(buffer, length));
            if (grown == nullptr) {
                ok = false;
                break;
            }
            buffer   = grown;
            capacity = length;
        }
        if (nvs_get_blob(src, keys[i], buffer, &length) != ESP_OK ||
            nvs_set_blob(dst, keys[i], buffer, length) != ESP_OK) {
            ok = false;
            break;
        }
    }

    free(buffer);
    if (ok) {
        ok = nvs_commit(dst) == ESP_OK;
    }
    nvs_close(dst);
    nvs_close(src);
    return ok;
}

} // namespace

uint32_t ble_identity_hash(const char* identity) {
    // FNV-1a, 32-bit.
    uint32_t hash = 2166136261u;
    for (const char* p = identity; p != nullptr && *p != '\0'; ++p) {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 16777619u;
    }
    // 0 means "no owner recorded" and 0xFFFFFFFF means "switch in progress".
    if (hash == 0 || hash == OWNER_TRANSITION) {
        hash ^= 0x5A5A5A5Au;
    }
    return hash;
}

bool ble_identity_derive_addr(uint32_t identity_hash, const uint8_t base[BT_ADDR_LEN],
                              uint8_t out[BT_ADDR_LEN]) {
    if (base == nullptr || out == nullptr) {
        return false;
    }

    memcpy(out, base, BT_ADDR_LEN);
    // Only the identity hash bytes are mixed in, so the same controller + identity always
    // yields the same address (and therefore the same peer entry on the PC).
    out[0] ^= static_cast<uint8_t>(identity_hash);
    out[1] ^= static_cast<uint8_t>(identity_hash >> 8);
    out[2] ^= static_cast<uint8_t>(identity_hash >> 16);
    out[3] ^= static_cast<uint8_t>(identity_hash >> 24);
    // Static random address: the two most significant bits of the MSB must both be set.
    out[5] = static_cast<uint8_t>(0xC0u | (out[5] & 0x3Fu));
    // Never collide with the public address: it would defeat the whole point (and the peer
    // would silently reuse its existing bond).
    if (memcmp(out, base, BT_ADDR_LEN) == 0) {
        out[4] ^= 0x01u;
    }
    return true;
}

static bool activate_bond_slot(uint32_t identity_hash) {
    uint32_t owner = 0;
    if (!read_owner(&owner)) {
        // No marker means the active namespace predates per-identity slots, so its bonds were
        // made with the public address and are unusable now. Start clean.
        if (!clear_namespace(BOND_NAMESPACE)) {
            LOG_E(TAG, "could not clear stale bonds");
            return false;
        }
        if (!write_owner(identity_hash)) {
            LOG_E(TAG, "could not record bond slot owner");
            return false;
        }
        LOG_I(TAG, "bond slots initialised for identity %08lx", (unsigned long)identity_hash);
        return true;
    }

    if (owner == identity_hash) {
        return true;
    }

    // Archive the active set and commit the transition marker before touching it. A power loss
    // in between leaves the marker behind, so the next boot reinstalls the archive instead of
    // filing it under the wrong identity.
    if (owner != OWNER_TRANSITION) {
        char slot[SLOT_NAMESPACE_SIZE];
        get_slot_namespace(owner, slot);
        if (!copy_namespace(BOND_NAMESPACE, slot)) {
            LOG_E(TAG, "failed to archive bonds of identity %08lx; keeping its slot active",
                  (unsigned long)owner);
            return false;
        }
        if (!write_owner(OWNER_TRANSITION)) {
            LOG_E(TAG, "failed to record bond slot transition; keeping identity %08lx active",
                  (unsigned long)owner);
            return false;
        }
    }

    char slot[SLOT_NAMESPACE_SIZE];
    get_slot_namespace(identity_hash, slot);

    // copy_namespace() fails both when the identity never archived a set and when a copy failed.
    // Only the former may clear the active set and commit the new owner. A failed restore keeps
    // OWNER_TRANSITION, so the next activation retries it instead of archiving an empty set over
    // the identity's intact archive.
    bool restored;
    nvs_handle_t probe;
    esp_err_t probe_result = nvs_open(slot, NVS_READONLY, &probe);
    if (probe_result == ESP_OK) {
        nvs_close(probe);
        restored = copy_namespace(slot, BOND_NAMESPACE);
    } else if (probe_result == ESP_ERR_NVS_NOT_FOUND) {
        // A first pairing for this identity, so there is nothing to restore.
        restored = clear_namespace(BOND_NAMESPACE);
    } else {
        restored = false;
    }
    if (!restored) {
        // The marker stays at OWNER_TRANSITION, so the next attempt retries the restore.
        LOG_E(TAG, "failed to activate bond slot for identity %08lx", (unsigned long)identity_hash);
        return false;
    }
    if (!write_owner(identity_hash)) {
        LOG_E(TAG, "failed to record bond slot owner %08lx", (unsigned long)identity_hash);
        return false;
    }
    LOG_I(TAG, "bond slot switched to identity %08lx", (unsigned long)identity_hash);
    return true;
}

namespace {

struct ActivateJob {
    uint32_t identityHash;
    bool result;
};

/** File operation: it runs on the file operations task, which can write flash from any caller's request */
void activateInBackground(void* context) {
    auto* job = static_cast<ActivateJob*>(context);
    job->result = activate_bond_slot(job->identityHash);
}

}

bool ble_run_file_operation(file_ops_work_fn_t work, void* context) {
    TaskEventGroup group {};
    task_event_group_construct(&group);
    FileOpsSubscription subscription {};
    bool queued = false;
    if (file_ops_subscribe(&subscription, &group) == ERROR_NONE) {
        if (file_ops_queue(work, context, &subscription) == ERROR_NONE) {
            // The context usually lives on the caller's stack, so the work has to finish before returning
            queued = file_ops_await(&subscription, portMAX_DELAY) == ERROR_NONE;
        }
        file_ops_unsubscribe(&subscription);
    }
    task_event_group_destruct(&group);
    return queued;
}

bool ble_identity_bond_slot_activate(uint32_t identity_hash) {
    ActivateJob job { identity_hash, false };
    if (!ble_run_file_operation(activateInBackground, &job)) {
        LOG_E(TAG, "could not run bond slot activation");
        return false;
    }
    return job.result;
}

#endif // CONFIG_BT_NIMBLE_ENABLED
