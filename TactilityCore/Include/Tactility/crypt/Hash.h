#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Implementation of DJB2 hashing algorithm.
 * @param[in] str the string to calculate the hash for
 * @return the hash
 */
uint32_t djb2_str(const char* str);

/**
 * Implementation of DJB2 hashing algorithm.
 * @param[in] data the bytes to calculate the hash for
 * @param[in] length the size of data
 * @return the hash
 */
uint32_t djb2_data(const void* data, size_t length);

#ifdef __cplusplus
}
#endif
