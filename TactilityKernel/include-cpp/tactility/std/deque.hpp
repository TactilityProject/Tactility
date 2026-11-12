// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/Allocator.hpp>

#include <deque>

namespace tt::std {

/** std::deque that prefers external memory (PSRAM) and falls back to internal RAM */
template<typename T>
using deque = ::std::deque<T, tt::OptExternalAllocator<T>>;

} // namespace tt::std
