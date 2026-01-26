#include <Tactility/crypt/Crypt.h>

#include <Tactility/Check.h>
#include <Tactility/Logger.h>

#include <mbedtls/aes.h>
#include <cstring>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#endif

namespace tt::crypt {

static const auto LOGGER = Logger("Crypt");

#define TT_NVS_NAMESPACE "tt_secure"

#ifdef ESP_PLATFORM
/**
 * Get a key based on hardware parameters.
 * @param[out] key the output key
 */
static void get_hardware_key(uint8_t key[32]) {
    uint8_t mac[8];
    // MAC can be 6 or 8 bytes
    size_t mac_length = esp_mac_addr_len_get(ESP_MAC_EFUSE_FACTORY);
    LOGGER.info("Using MAC with length {}", mac_length);
    check(mac_length <= 8);
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY));

    // Fill buffer with repeating MAC
    for (size_t i = 0; i < 32; ++i) {
        key[i] = mac[i % mac_length];
    }
}
#endif

#ifdef ESP_PLATFORM
/**
 * The key is built up as follows:
 * - Fetch 32 bytes from NVS storage and store as key data
 * - Fetch 6-8 MAC bytes and overwrite the first 6-8 bytes of the key with this info
 *
 * When flash encryption is disabled:
 *   Without the MAC data, an attack would look like this:
 *   - Retrieve all the partitions from the ESP32
 *   - Read the key from NVS flash
 *   - Use the key to decrypt
 *   With the MAC data added, an attacker would have to do much more:
 *   - Retrieve all the partitions from the ESP32 (copy app)
 *   - Upload custom app to retrieve internal MAC
 *   - Read the key from NVS flash
 *   - Re-flash original app and combine it with the MAC
 *   - Use the key to decrypt
 *   - Re-flash the device with original firmware.
 *
 * Adding the MAC doesn't add a lot of extra security, but I think it's worth it.
 *
 * @param[out] key the output key
 */
static void get_nvs_key(uint8_t key[32]) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open(TT_NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (result != ESP_OK) {
        LOGGER.error("Failed to get key from NVS ({})", esp_err_to_name(result));
        check(false, "NVS error");
    }

    size_t length = 32;
    if (nvs_get_blob(handle, "key", key, &length) == ESP_OK) {
        LOGGER.info("Fetched key from NVS ({} bytes)", length);
        check(length == 32);
    } else {
        // TODO: Improved randomness
        esp_cpu_cycle_count_t cycle_count = esp_cpu_get_cycle_count();
        auto seed = cycle_count;
        srand(seed);
        for (int i = 0; i < 32; ++i) {
            key[i] = (uint8_t)(rand());
        }
        ESP_ERROR_CHECK(nvs_set_blob(handle, "key", key, 32));
        LOGGER.info("Stored new key in NVS");
    }

    nvs_close(handle);
}
#endif

/**
 * Performs XOR on 2 memory regions and stores it in a third
 * @param[in] inLeft input buffer for XOR
 * @param[in] inRight second input buffer for XOR
 * @param[out] out output buffer for result of XOR
 * @param[in] length data length (all buffers must be at least this size)
 */
static void xorKey(const uint8_t* inLeft, const uint8_t* inRight, uint8_t* out, size_t length) {
    for (int i = 0; i < length; ++i) {
        out[i] = inLeft[i] ^ inRight[i];
    }
}

/**
 * Combines a stored key and a hardware key into a single reliable key value.
 * @param[out] key the key output
 */
static void getKey(uint8_t key[32]) {
#if !defined(CONFIG_SECURE_BOOT) || !defined(CONFIG_SECURE_FLASH_ENC_ENABLED)
    LOGGER.warn("Using tt_secure_* code with secure boot and/or flash encryption disabled.");
    LOGGER.warn("An attacker with physical access to your ESP32 can decrypt your secure data.");
#endif

#ifdef ESP_PLATFORM
    uint8_t hardware_key[32];
    uint8_t nvs_key[32];

    get_hardware_key(hardware_key);
    get_nvs_key(nvs_key);
    xorKey(hardware_key, nvs_key, key, 32);
#else
    LOGGER.warn("Using unsafe key for debugging purposes.");
    memset(key, 0, 32);
#endif
}

void getIv(const void* data, size_t dataLength, uint8_t iv[16]) {
    memset(iv, 0, 16);
    auto* data_bytes = (uint8_t*)data;
    for (int i = 0; i < dataLength; ++i) {
        size_t safe_index = i % 16;
        iv[safe_index] %= data_bytes[i];
    }
}

static int aes256CryptCbc(
    const uint8_t key[32],
    int mode,
    size_t length,
    unsigned char iv[16],
    const unsigned char* input,
    unsigned char* output
) {
    check(key && iv && input && output);

    if ((length % 16) || (length == 0)) {
        return -1; // TODO: Proper error code from mbed lib?
    }

    mbedtls_aes_context master;
    mbedtls_aes_init(&master);
    if (mode == MBEDTLS_AES_ENCRYPT) {
        mbedtls_aes_setkey_enc(&master, key, 256);
    } else {
        mbedtls_aes_setkey_dec(&master, key, 256);
    }
    int result = mbedtls_aes_crypt_cbc(&master, mode, length, iv, input, output);
    mbedtls_aes_free(&master);
    return result;
}

int encrypt(const uint8_t iv[16], const uint8_t* inData, uint8_t* outData, size_t dataLength) {
    check(dataLength % 16 == 0, "Length is not a multiple of 16 bytes (for AES 256");
    uint8_t key[32];
    getKey(key);

    // TODO: Is this still needed after switching to regular AES functions?
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, sizeof(iv_copy));

    return aes256CryptCbc(key, MBEDTLS_AES_ENCRYPT, dataLength, iv_copy, inData, outData);
}

int decrypt(const uint8_t iv[16], const uint8_t* inData, uint8_t* outData, size_t dataLength) {
    check(dataLength % 16 == 0, "Length is not a multiple of 16 bytes (for AES 256");
    uint8_t key[32];
    getKey(key);

    // TODO: Is this still needed after switching to regular AES functions?
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, sizeof(iv_copy));

    return aes256CryptCbc(key, MBEDTLS_AES_DECRYPT, dataLength, iv_copy, inData, outData);
}

} // namespace
