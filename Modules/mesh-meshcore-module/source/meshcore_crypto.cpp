// SPDX-License-Identifier: Apache-2.0
#include <mesh_meshcore/meshcore_crypto.h>

#include <psa/crypto.h>

#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_random.h>
#else
#include <random>
#endif

namespace {

psa_key_id_t importKey(psa_key_type_t type, psa_algorithm_t algorithm, psa_key_usage_t usage, const uint8_t* key, size_t length) {
    psa_crypto_init();
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_set_key_usage_flags(&attributes, usage);
    psa_key_id_t id = PSA_KEY_ID_NULL;
    if (psa_import_key(&attributes, key, length, &id) != PSA_SUCCESS) {
        return PSA_KEY_ID_NULL;
    }
    return id;
}

void hmac(const uint8_t secret[MESHCORE_SECRET_SIZE], const uint8_t* data, size_t length, uint8_t mac[32]) {
    // On failure the MAC is all zeros, which doesn't match any valid MAC
    meshcore_hmac_sha256(secret, MESHCORE_SECRET_SIZE, data, length, mac);
}

/** AES-128-ECB over whole blocks */
bool aesEcb(const uint8_t secret[MESHCORE_SECRET_SIZE], bool encrypt, const uint8_t* input, uint8_t* output, size_t length) {
    const psa_key_id_t key = importKey(
        PSA_KEY_TYPE_AES,
        PSA_ALG_ECB_NO_PADDING,
        encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT,
        secret,
        MESHCORE_CIPHER_KEY_SIZE
    );
    if (key == PSA_KEY_ID_NULL) {
        return false;
    }
    size_t outputLength = 0;
    const psa_status_t status = encrypt
        ? psa_cipher_encrypt(key, PSA_ALG_ECB_NO_PADDING, input, length, output, length, &outputLength)
        : psa_cipher_decrypt(key, PSA_ALG_ECB_NO_PADDING, input, length, output, length, &outputLength);
    psa_destroy_key(key);
    return status == PSA_SUCCESS && outputLength == length;
}

} // namespace

bool meshcore_hmac_sha256(const uint8_t* secret, size_t secret_length, const uint8_t* data, size_t length, uint8_t mac[32]) {
    const auto algorithm = PSA_ALG_HMAC(PSA_ALG_SHA_256);
    const psa_key_id_t key = importKey(PSA_KEY_TYPE_HMAC, algorithm, PSA_KEY_USAGE_SIGN_MESSAGE, secret, secret_length);
    size_t macLength = 0;
    const bool ok = key != PSA_KEY_ID_NULL && psa_mac_compute(key, algorithm, data, length, mac, 32, &macLength) == PSA_SUCCESS;
    if (!ok) {
        memset(mac, 0, 32);
    }
    psa_destroy_key(key);
    return ok;
}

void meshcore_sha256(uint8_t* hash, size_t hash_size, const uint8_t* data, size_t length) {
    meshcore_sha256_pair(hash, hash_size, data, length, nullptr, 0);
}

void meshcore_sha256_pair(uint8_t* hash, size_t hash_size, const uint8_t* first, size_t first_length, const uint8_t* second, size_t second_length) {
    psa_crypto_init();
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    uint8_t full[32] = {};
    size_t fullLength = 0;
    psa_hash_setup(&operation, PSA_ALG_SHA_256);
    psa_hash_update(&operation, first, first_length);
    if (second_length > 0) {
        psa_hash_update(&operation, second, second_length);
    }
    psa_hash_finish(&operation, full, sizeof(full), &fullLength);
    memcpy(hash, full, hash_size < sizeof(full) ? hash_size : sizeof(full));
}

size_t meshcore_encrypt_then_mac(const uint8_t secret[MESHCORE_SECRET_SIZE], uint8_t* destination, const uint8_t* source, size_t length) {
    const size_t blocks = (length + MESHCORE_CIPHER_BLOCK_SIZE - 1) / MESHCORE_CIPHER_BLOCK_SIZE;
    const size_t encryptedLength = blocks * MESHCORE_CIPHER_BLOCK_SIZE;
    uint8_t padded[256] = {};
    if (encryptedLength > sizeof(padded)) {
        return 0;
    }
    memcpy(padded, source, length);
    uint8_t* ciphertext = destination + MESHCORE_CIPHER_MAC_SIZE;
    if (!aesEcb(secret, true, padded, ciphertext, encryptedLength)) {
        return 0;
    }
    uint8_t mac[32];
    hmac(secret, ciphertext, encryptedLength, mac);
    memcpy(destination, mac, MESHCORE_CIPHER_MAC_SIZE);
    return MESHCORE_CIPHER_MAC_SIZE + encryptedLength;
}

size_t meshcore_mac_then_decrypt(const uint8_t secret[MESHCORE_SECRET_SIZE], uint8_t* destination, const uint8_t* source, size_t length) {
    if (length <= MESHCORE_CIPHER_MAC_SIZE) {
        return 0;
    }
    const uint8_t* ciphertext = source + MESHCORE_CIPHER_MAC_SIZE;
    const size_t ciphertextLength = length - MESHCORE_CIPHER_MAC_SIZE;
    if (ciphertextLength % MESHCORE_CIPHER_BLOCK_SIZE != 0) {
        return 0;
    }
    uint8_t mac[32];
    hmac(secret, ciphertext, ciphertextLength, mac);
    if (memcmp(mac, source, MESHCORE_CIPHER_MAC_SIZE) != 0) {
        return 0;
    }
    return aesEcb(secret, false, ciphertext, destination, ciphertextLength) ? ciphertextLength : 0;
}

void meshcore_random(uint8_t* buffer, size_t length) {
#ifdef ESP_PLATFORM
    esp_fill_random(buffer, length);
#else
    std::random_device device;
    for (size_t i = 0; i < length; i++) {
        buffer[i] = static_cast<uint8_t>(device());
    }
#endif
}
