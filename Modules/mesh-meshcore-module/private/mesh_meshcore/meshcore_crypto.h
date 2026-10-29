// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t MESHCORE_PUBLIC_KEY_SIZE = 32;
constexpr size_t MESHCORE_PRIVATE_KEY_SIZE = 64;
constexpr size_t MESHCORE_SIGNATURE_SIZE = 64;
constexpr size_t MESHCORE_SECRET_SIZE = 32;
constexpr size_t MESHCORE_CIPHER_KEY_SIZE = 16;
constexpr size_t MESHCORE_CIPHER_MAC_SIZE = 2;
constexpr size_t MESHCORE_CIPHER_BLOCK_SIZE = 16;

/** HMAC-SHA256 with a key of any length */
void meshcore_hmac_sha256(const uint8_t* key, size_t key_length, const uint8_t* data, size_t length, uint8_t mac[32]);

void meshcore_sha256(uint8_t* hash, size_t hash_size, const uint8_t* data, size_t length);

/** SHA-256 over two concatenated inputs, truncated to @a hash_size */
void meshcore_sha256_pair(uint8_t* hash, size_t hash_size, const uint8_t* first, size_t first_length, const uint8_t* second, size_t second_length);

/**
 * AES-128-ECB encrypt @a source (zero-padded to whole blocks) with the first 16 bytes of @a secret, then
 * prefix the first 2 bytes of an HMAC-SHA256 over the ciphertext keyed with the whole @a secret.
 * @return the number of bytes written to @a destination (MAC + ciphertext)
 */
size_t meshcore_encrypt_then_mac(const uint8_t secret[MESHCORE_SECRET_SIZE], uint8_t* destination, const uint8_t* source, size_t length);

/**
 * Verify the MAC and decrypt. @a length includes the MAC.
 * @return the number of plaintext bytes (whole blocks, may include zero padding), 0 when the MAC doesn't match
 */
size_t meshcore_mac_then_decrypt(const uint8_t secret[MESHCORE_SECRET_SIZE], uint8_t* destination, const uint8_t* source, size_t length);

/** Fill @a buffer with random bytes */
void meshcore_random(uint8_t* buffer, size_t length);
