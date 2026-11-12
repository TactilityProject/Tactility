// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/Allocator.hpp>

#include <functional>
#include <map>
#include <utility>

namespace tt::std {

/** std::map that prefers external memory (PSRAM) and falls back to internal RAM */
template<typename Key, typename Value>
using map = ::std::map<Key, Value, ::std::less<Key>, tt::OptExternalAllocator<::std::pair<const Key, Value>>>;

} // namespace tt::std
