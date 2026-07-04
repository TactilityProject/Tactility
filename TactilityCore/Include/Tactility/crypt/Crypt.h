/** @file Crypt.h
 *
 * @brief Hardware-bound encryption methods.
 * @warning Enable secure boot and flash encryption to increase security.
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
 * @brief Fills the IV with zeros and then creates an IV based on the input data.
 * @param[in] data input data
 * @param[in] dataLength input data length
 * @param[out] iv output IV
 */
void crypt_get_iv(const void* data, size_t dataLength, uint8_t iv[16]);

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
 * @return the result of esp_aes_crypt_cbc() (MBEDTLS_ERR_*)
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
 * @return the result of esp_aes_crypt_cbc() (MBEDTLS_ERR_*)
 */
int crypt_decrypt(const uint8_t iv[16], const uint8_t* inData, uint8_t* outData, size_t dataLength);

#ifdef __cplusplus
}
#endif
