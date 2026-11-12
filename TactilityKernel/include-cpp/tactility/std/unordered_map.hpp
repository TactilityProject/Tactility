// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/Allocator.hpp>

#include <functional>
#include <unordered_map>
#include <utility>

namespace tt::std {

/** std::unordered_map that prefers external memory (PSRAM) and falls back to internal RAM */
template<typename Key, typename Value, typename Hash = ::std::hash<Key>, typename KeyEqual = ::std::equal_to<Key>>
using unordered_map = ::std::unordered_map<Key, Value, Hash, KeyEqual, tt::OptExternalAllocator<::std::pair<const Key, Value>>>;

} // namespace tt::std
