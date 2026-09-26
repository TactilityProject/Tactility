// SPDX-License-Identifier: Apache-2.0
#include <functional>

#ifdef ESP_PLATFORM

// Out-of-line definitions for the std::function<long()> members that module.cpp exports.
// On ESP32, int32_t is long, so this is Thread::MainFunction. Nothing else is guaranteed to emit
// these, so they are explicitly instantiated here.
// Kept out of module.cpp: it declares these same symbols by their mangled names as extern "C".
template long std::function<long()>::operator()() const;
template std::function<long()>::function(const std::function<long()>&);

#endif
