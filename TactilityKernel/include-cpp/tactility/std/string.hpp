// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/Allocator.hpp>

#include <string>

namespace tt::std {

/** std::string that prefers external memory (PSRAM) and falls back to internal RAM */
using string = ::std::basic_string<char, ::std::char_traits<char>, tt::OptExternalAllocator<char>>;

} // namespace tt::std
