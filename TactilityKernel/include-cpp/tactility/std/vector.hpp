// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/Allocator.hpp>

#include <vector>

namespace tt::std {

/** std::vector that prefers external memory (PSRAM) and falls back to internal RAM */
template<typename T>
using vector = ::std::vector<T, tt::OptExternalAllocator<T>>;

} // namespace tt::std
