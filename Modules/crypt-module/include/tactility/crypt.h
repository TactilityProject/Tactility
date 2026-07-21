// SPDX-License-Identifier: Apache-2.0
/** @file crypt.h
 *
 * @brief Encryption helper functions.
 *
 * Offers AES 256 CBC encryption with built-in key.
 * The key is built from data including:
 *  - the internal factory MAC address
 *  - random data stored in NVS
 *
 * It's important to use flash encryption to avoid an attacker to get
 * access to your encrypted data. If flash encryption is disabled,
 * someone can fetch the key from the partitions.
 *
 * See:
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Deterministically derives an IV from the given data (the first 16 bytes of its SHA-256 hash).
 *
 * Calling this again with the same data always produces the same IV - use this when there's nowhere
 * to store a per-encryption IV alongside the ciphertext, and the caller can supply the same associated
 * data (e.g. an identifier that's known at both encrypt and decrypt time, not the secret itself) on both
 * ends. Because the IV doesn't change between encryptions with the same associated data, encrypting the
 * same plaintext twice produces the same ciphertext - prefer crypt_generate_iv() when ciphertext can be
 * stored alongside a random IV instead.
 *
 * @param[in] data input data
 * @param[in] dataLength input data length
 * @param[out] iv output IV
 */
void crypt_get_iv(const void* data, size_t dataLength, uint8_t iv[16]);

/**
 * @brief Fills the IV with cryptographically secure random bytes.
 *
 * Use this when the IV can be stored alongside the ciphertext (e.g. prefixed to it) and read back for
 * decryption. This gives every encryption operation a unique, unpredictable IV, which is the standard
 * and strongest way to use crypt_encrypt()/crypt_decrypt().
 *
 * @param[out] iv output IV
 */
void crypt_generate_iv(uint8_t iv[16]);

/**
 * @brief Encrypt data.
 *
 * Important: Use flash encryption to increase security.
 * Important: input and output data must be aligned to 16 bytes.
 *
 * @param[in] iv the AES IV
 * @param[in] inData input data
 * @param[out] outData output data
 * @param[in] dataLength data length, a multiple of 16 (for both inData and outData)
 * @return the result of esp_aes_crypt_cbc() (MBEDTLS_ERR_*), or -1 if dataLength is not a positive multiple of 16
 */
int crypt_encrypt(const uint8_t iv[16], const uint8_t* inData, uint8_t* outData, size_t dataLength);

/**
 * @brief Decrypt data.
 *
 * Important: Use flash encryption to increase security.
 * Important: input and output data must be aligned to 16 bytes.
 *
 * @param[in] iv AES IV
 * @param[in] inData input data
 * @param[out] outData output data
 * @param[in] dataLength data length, a multiple of 16 (for both inData and outData)
 * @return the result of esp_aes_crypt_cbc() (MBEDTLS_ERR_*), or -1 if dataLength is not a positive multiple of 16
 */
int crypt_decrypt(const uint8_t iv[16], const uint8_t* inData, uint8_t* outData, size_t dataLength);

#ifdef __cplusplus
}
#endif
