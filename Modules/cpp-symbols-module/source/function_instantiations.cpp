// SPDX-License-Identifier: Apache-2.0
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifdef ESP_PLATFORM

// Out-of-line definitions for the std::function<long()> members that module.cpp exports.
// On ESP32, int32_t is long, so this is Thread::MainFunction. Nothing else is guaranteed to emit
// these, so they are explicitly instantiated here.
// Kept out of module.cpp: it declares these same symbols by their mangled names as extern "C".
template long std::function<long()>::operator()() const;
template std::function<long()>::function(const std::function<long()>&);

// operator+(string&&, string&&) is exported by module.cpp too. It is an inline template, so it only exists
// when some code in the firmware calls it. Instantiated here, so the module doesn't depend on that.
template std::string std::operator+(std::string&&, std::string&&);

// The other string operators and helpers that module.cpp exports, for the same reason
template std::string std::operator+(const std::string&, const char*);
template bool std::operator==(const std::string&, const std::string&);
template std::string std::__str_concat<std::string>(const char*, std::string::size_type, const char*, std::string::size_type, const std::string::allocator_type&);

// std::map<string, string> and the vectors that module.cpp exports. The firmware only emits these
// out of line when its own code happens not to inline them, which depends on the optimization level.
template class std::vector<unsigned char>;
template class std::vector<std::string>;

using StringPair = std::pair<const std::string, std::string>;
using StringMapTree = std::_Rb_tree<std::string, StringPair, std::_Select1st<StringPair>, std::less<std::string>, std::allocator<StringPair>>;
template class std::_Rb_tree<std::string, StringPair, std::_Select1st<StringPair>, std::less<std::string>, std::allocator<StringPair>>;
template class std::map<std::string, std::string>;
template StringPair::pair(std::piecewise_construct_t, std::tuple<const std::string&>, std::tuple<>);

#endif
