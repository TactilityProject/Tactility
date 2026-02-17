#include <private/elf_symbol.h>
#include <cstddef>
#include <cstdlib>

#include <symbols/mbedtls.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/cipher.h>
#include <mbedtls/md.h>
#include <mbedtls/bignum.h>
#include <mbedtls/rsa.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/error.h>

// mbedtls_calloc/mbedtls_free may be macros (expanding to calloc/free).
// Provide wrapper functions so the ELF symbol names resolve correctly.
extern "C" {
static void* tt_mbedtls_calloc(size_t n, size_t size) { return calloc(n, size); }
static void  tt_mbedtls_free(void* ptr) { free(ptr); }
}

// mbedtls_pk_load_file is declared in pk_internal.h (not a public header)
extern "C" int mbedtls_pk_load_file(const char *path, unsigned char **buf, size_t *n);

const esp_elfsym mbedtls_symbols[] = {
    // Platform (wrappers to avoid macro issues)
    { "mbedtls_calloc", (void*)&tt_mbedtls_calloc },
    { "mbedtls_free",   (void*)&tt_mbedtls_free },
    // CTR_DRBG (random number generation)
    ESP_ELFSYM_EXPORT(mbedtls_ctr_drbg_init),
    ESP_ELFSYM_EXPORT(mbedtls_ctr_drbg_free),
    ESP_ELFSYM_EXPORT(mbedtls_ctr_drbg_seed),
    ESP_ELFSYM_EXPORT(mbedtls_ctr_drbg_random),
    // Entropy
    ESP_ELFSYM_EXPORT(mbedtls_entropy_init),
    ESP_ELFSYM_EXPORT(mbedtls_entropy_free),
    ESP_ELFSYM_EXPORT(mbedtls_entropy_func),
    // Cipher
    ESP_ELFSYM_EXPORT(mbedtls_cipher_init),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_free),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_setup),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_setkey),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_set_iv),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_reset),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_update),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_finish),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_get_block_size),
    ESP_ELFSYM_EXPORT(mbedtls_cipher_info_from_type),
    // Message digest / HMAC
    ESP_ELFSYM_EXPORT(mbedtls_md),
    ESP_ELFSYM_EXPORT(mbedtls_md_init),
    ESP_ELFSYM_EXPORT(mbedtls_md_free),
    ESP_ELFSYM_EXPORT(mbedtls_md_setup),
    ESP_ELFSYM_EXPORT(mbedtls_md_starts),
    ESP_ELFSYM_EXPORT(mbedtls_md_update),
    ESP_ELFSYM_EXPORT(mbedtls_md_finish),
    ESP_ELFSYM_EXPORT(mbedtls_md_hmac_starts),
    ESP_ELFSYM_EXPORT(mbedtls_md_hmac_update),
    ESP_ELFSYM_EXPORT(mbedtls_md_hmac_finish),
    ESP_ELFSYM_EXPORT(mbedtls_md_info_from_type),
    // Bignum (MPI)
    ESP_ELFSYM_EXPORT(mbedtls_mpi_init),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_free),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_read_binary),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_write_binary),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_size),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_bitlen),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_lset),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_set_bit),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_fill_random),
    ESP_ELFSYM_EXPORT(mbedtls_mpi_exp_mod),
    // RSA
    ESP_ELFSYM_EXPORT(mbedtls_rsa_init),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_free),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_copy),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_get_len),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_check_pubkey),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_check_privkey),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_pkcs1_sign),
    ESP_ELFSYM_EXPORT(mbedtls_rsa_pkcs1_verify),
    // Public key abstraction
    ESP_ELFSYM_EXPORT(mbedtls_pk_init),
    ESP_ELFSYM_EXPORT(mbedtls_pk_free),
    ESP_ELFSYM_EXPORT(mbedtls_pk_get_type),
    ESP_ELFSYM_EXPORT(mbedtls_pk_parse_key),
    ESP_ELFSYM_EXPORT(mbedtls_pk_parse_keyfile),
    ESP_ELFSYM_EXPORT(mbedtls_pk_load_file),
    // ECP (elliptic curves)
    ESP_ELFSYM_EXPORT(mbedtls_ecp_group_load),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_point_init),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_point_free),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_point_read_binary),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_point_write_binary),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_check_pubkey),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_check_privkey),
    ESP_ELFSYM_EXPORT(mbedtls_ecp_mul),
    // ECDSA
    ESP_ELFSYM_EXPORT(mbedtls_ecdsa_init),
    ESP_ELFSYM_EXPORT(mbedtls_ecdsa_free),
    ESP_ELFSYM_EXPORT(mbedtls_ecdsa_genkey),
    ESP_ELFSYM_EXPORT(mbedtls_ecdsa_from_keypair),
    ESP_ELFSYM_EXPORT(mbedtls_ecdsa_sign),
    ESP_ELFSYM_EXPORT(mbedtls_ecdsa_verify),
    // ECDH
    ESP_ELFSYM_EXPORT(mbedtls_ecdh_compute_shared),
    // Error strings
    ESP_ELFSYM_EXPORT(mbedtls_strerror),
    // delimiter
    ESP_ELFSYM_END
};
