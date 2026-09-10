// SPDX-License-Identifier: Apache-2.0
// Tactility forces Newlib (see device.py) instead of ESP-IDF 6.x's Picolibc default, but
// managed_components/espressif__esp_ipa's prebuilt libesp_ipa.a (camera ISP auto-white-balance,
// linked in for boards with a camera sensor node, e.g. m5stack-tab5) was built against Picolibc
// and references its __issignalingf(), which Newlib doesn't provide.
#include <sdkconfig.h>
#if CONFIG_IDF_TARGET_ESP32P4

#include <stdint.h>

int __issignalingf(float x) {
    union { float f; uint32_t i; } u = { .f = x };
    if (((u.i >> 23) & 0xff) != 0xff) {
        return 0; // not NaN or Inf
    }
    // NaN with mantissa MSB clear is signaling; MSB set is quiet.
    return (u.i & 0x007fffff) != 0 && (u.i & 0x00400000) == 0;
}

#endif // CONFIG_IDF_TARGET_ESP32P4
