// SPDX-License-Identifier: Apache-2.0
#include <mbedtls/module.h>

// mbedTLS 4.x removed or privatized most legacy primitives with no public replacement (ctr_drbg,
// entropy, cipher, bignum, rsa, most of pk, ecp, ecdsa, ecdh - PSA Crypto is the sanctioned
// replacement now). True on both ESP-IDF's bundled mbedTLS and the simulator's vendored
// Libraries/mbedtls, now the same major version. Nothing in this repo consumes any of these
// symbols directly - this table is speculative ABI surface for side-loaded ELF apps - so it
// shrinks to what's still genuinely public: plain message-digest hashing and error strings.
// See ESP_IDF_6.1_BREAKING_CHANGES.md §6.1.
#include <mbedtls/md.h>
#include <mbedtls/error.h>

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    // Message digest
    DEFINE_MODULE_SYMBOL(mbedtls_md),
    DEFINE_MODULE_SYMBOL(mbedtls_md_init),
    DEFINE_MODULE_SYMBOL(mbedtls_md_free),
    DEFINE_MODULE_SYMBOL(mbedtls_md_setup),
    DEFINE_MODULE_SYMBOL(mbedtls_md_starts),
    DEFINE_MODULE_SYMBOL(mbedtls_md_update),
    DEFINE_MODULE_SYMBOL(mbedtls_md_finish),
    DEFINE_MODULE_SYMBOL(mbedtls_md_info_from_type),
    // Error strings
    DEFINE_MODULE_SYMBOL(mbedtls_strerror),
    MODULE_SYMBOL_TERMINATOR,
};

Module mbedtls_module = {
    .name = "mbedtls",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
