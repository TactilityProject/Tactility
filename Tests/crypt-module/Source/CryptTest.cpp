#include "doctest.h"
#include <tactility/crypt.h>
#include <cstring>

TEST_CASE("crypt_encrypt followed by crypt_decrypt returns the original data") {
    uint8_t iv[16];
    crypt_get_iv("test-seed", 9, iv);

    uint8_t plaintext[16];
    memcpy(plaintext, "0123456789abcdef", sizeof(plaintext));

    uint8_t encrypted[16];
    CHECK_EQ(crypt_encrypt(iv, plaintext, encrypted, sizeof(encrypted)), 0);

    // Re-derive the same IV, as a real caller would when decrypting later
    uint8_t iv2[16];
    crypt_get_iv("test-seed", 9, iv2);

    uint8_t decrypted[16];
    CHECK_EQ(crypt_decrypt(iv2, encrypted, decrypted, sizeof(decrypted)), 0);

    CHECK_EQ(memcmp(plaintext, decrypted, sizeof(plaintext)), 0);
}

TEST_CASE("crypt_encrypt with a length that isn't a multiple of 16 fails without crashing") {
    uint8_t iv[16] = {};
    uint8_t plaintext[15] = {};
    uint8_t encrypted[15] = {};
    CHECK_NE(crypt_encrypt(iv, plaintext, encrypted, sizeof(plaintext)), 0);
}

TEST_CASE("crypt_decrypt with a length that isn't a multiple of 16 fails without crashing") {
    uint8_t iv[16] = {};
    uint8_t ciphertext[15] = {};
    uint8_t decrypted[15] = {};
    CHECK_NE(crypt_decrypt(iv, ciphertext, decrypted, sizeof(ciphertext)), 0);
}

TEST_CASE("crypt_encrypt with a zero length fails without crashing") {
    uint8_t iv[16] = {};
    uint8_t plaintext[1] = {};
    uint8_t encrypted[1] = {};
    CHECK_NE(crypt_encrypt(iv, plaintext, encrypted, 0), 0);
}

TEST_CASE("crypt_get_iv is deterministic for the same input") {
    uint8_t iv1[16];
    uint8_t iv2[16];
    crypt_get_iv("same-input", 10, iv1);
    crypt_get_iv("same-input", 10, iv2);
    CHECK_EQ(memcmp(iv1, iv2, sizeof(iv1)), 0);
}
