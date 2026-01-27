# Install script for directory: /home/ken/Projects/Tactility/Libraries/mbedtls/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/aes.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/aria.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/asn1.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/asn1write.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/base64.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/bignum.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/build_info.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/camellia.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ccm.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/chacha20.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/chachapoly.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/check_config.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/cipher.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/cmac.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/compat-2.x.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_adjust_legacy_crypto.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_adjust_legacy_from_psa.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_adjust_psa_from_legacy.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_adjust_psa_superset_legacy.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_adjust_ssl.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_adjust_x509.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/config_psa.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/constant_time.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ctr_drbg.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/debug.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/des.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/dhm.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ecdh.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ecdsa.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ecjpake.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ecp.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/entropy.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/error.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/gcm.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/hkdf.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/hmac_drbg.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/lms.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/mbedtls_config.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/md.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/md5.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/memory_buffer_alloc.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/net_sockets.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/nist_kw.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/oid.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/pem.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/pk.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/pkcs12.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/pkcs5.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/pkcs7.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/platform.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/platform_time.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/platform_util.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/poly1305.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/private_access.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/psa_util.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ripemd160.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/rsa.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/sha1.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/sha256.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/sha3.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/sha512.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ssl.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ssl_cache.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ssl_ciphersuites.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ssl_cookie.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/ssl_ticket.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/threading.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/timing.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/version.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/x509.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/x509_crl.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/x509_crt.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/mbedtls/x509_csr.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/psa" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/build_info.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_adjust_auto_enabled.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_adjust_config_key_pair_types.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_adjust_config_synonyms.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_builtin_composites.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_builtin_key_derivation.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_builtin_primitives.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_compat.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_config.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_driver_common.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_driver_contexts_composites.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_driver_contexts_key_derivation.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_driver_contexts_primitives.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_extra.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_legacy.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_platform.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_se_driver.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_sizes.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_struct.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_types.h"
    "/home/ken/Projects/Tactility/Libraries/mbedtls/include/psa/crypto_values.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/ken/Projects/Tactility/build_tests/Libraries/mbedtls/include/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
