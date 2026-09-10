// SPDX-License-Identifier: Apache-2.0
#include <cpp_symbols/module.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <utility>

#if defined(__GLIBCXX__) || defined(ESP_PLATFORM)
#define TT_CPP_SYMBOLS_AVAILABLE 1
#include <bits/functexcept.h>
#else
#define TT_CPP_SYMBOLS_AVAILABLE 0
#endif

#if TT_CPP_SYMBOLS_AVAILABLE
extern "C" {
    // cplusplus: compiler/runtime ABI support
#ifdef ESP_PLATFORM
    // Mangled for a 32-bit ABI ("j" = unsigned int, i.e. size_t on ESP32's ILP32). A 64-bit host's
    // libstdc++ exports these under different (m-suffixed) mangled names, so they don't apply there.
    extern void* _Znwj(uint32_t size); // operator new(unsigned int)
    extern void _ZdlPvj(void* p, uint64_t size); // operator delete(void*, unsigned int)
#endif
    extern void __cxa_pure_virtual();
    // cxx_guards.cpp
    extern int __cxa_guard_acquire(void* pg);
    extern void __cxa_guard_release(void* pg) throw();
    extern void __cxa_guard_abort(void* pg) throw();
#ifdef ESP_PLATFORM
    // Not part of the Itanium C++ ABI that desktop libstdc++ implements; ESP-IDF's toolchain only.
    extern void __cxa_guard_dummy(void);
#endif

    // stl: std::map / std::set red-black tree non-template helpers. We use the mangled names
    // directly (same pattern as the basic_string cold path below) to avoid ambiguity from the
    // overloaded const/non-const variants in stl_tree.h.
    void* _ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base(void*);
    void* _ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base(void*);
    void  _ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_(bool, void*, void*, void*);

#ifdef ESP_PLATFORM
    // string - same 32-bit-ABI mangling caveat as operator new/delete above.
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE15_M_replace_coldEPcjPKcjj(void*, char*, unsigned int, char const*, unsigned int, unsigned int);
    void* _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6substrEjj(void*, const void*, unsigned int, unsigned int);
    char* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_createERjj(void*, unsigned int*, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7reserveEj(void*, unsigned int);
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEcj(const void*, char, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_disposeEv(void*);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_replaceEjjPKcj(void*, unsigned int, unsigned int, const char*, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructIPKcEEvT_S8_St20forward_iterator_tag(void*, const char*, const char*, char);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEPKc(void*, const char*);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEPKcj(void*, const char*, unsigned int);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6assignEPKc(void*, const char*);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7_S_copyEPcPKcj(char*, const char*, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7_S_moveEPcPKcj(char*, const char*, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8_M_eraseEjj(void*, unsigned int, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8pop_backEv(void*);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_appendEPKcj(void*, const char*, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_assignERKS4_(void*, const void*);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEjjPKcj(void*, unsigned int, unsigned int, const char*, unsigned int);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9push_backEc(void*, char);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEaSEOS4_(void*, void*);
    // Non-members: return basic_string<char> by value, so the first param is the hidden
    // return-value pointer (Itanium ABI), same convention as substr() above.
    void* _ZSt12__str_concatINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEET_PKNS6_10value_typeENS6_9size_typeES9_SA_RKNS6_14allocator_typeE(void*, const char*, unsigned int, const char*, unsigned int, const void*);
    void* _ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EERKS8_PKS5_(void*, const void*, const char*);
#endif
}

#ifdef ESP_PLATFORM
namespace {
// basic_string(basic_string const&, pos, len) has no out-of-line definition anywhere to take the
// address of - unlike everything else above, GCC never emits one for this ctor under C++20+ (it's
// a pure header-inline forwarder around _M_construct(), confirmed by trying to force an
// instantiation and finding no resulting linkable symbol). Implemented directly instead: this
// function's placement-new triggers the compiler to inline the real construction logic here,
// producing a genuine addressable definition, registered below under the mangled name(s) the ELF
// loader actually looks up.
void construct_basic_string_from_substring(void* self, const void* str, unsigned int pos, unsigned int len) {
    new (self) std::string(*static_cast<const std::string*>(str), pos, len);
}
// Same story for basic_string(const char*, allocator<char> const&).
void construct_basic_string_from_cstr(void* self, const char* s, const void* alloc) {
    new (self) std::string(s, *static_cast<const std::allocator<char>*>(alloc));
}
// Same story for the move constructor, basic_string(basic_string&&).
void construct_basic_string_move(void* self, void* other) {
    new (self) std::string(std::move(*static_cast<std::string*>(other)));
}
}
#endif
#endif

static const ModuleSymbol SYMBOLS[] = {
#if TT_CPP_SYMBOLS_AVAILABLE
    // cplusplus
#ifdef ESP_PLATFORM
    DEFINE_MODULE_SYMBOL(_Znwj), // operator new(unsigned int)
    DEFINE_MODULE_SYMBOL(_ZdlPvj), // operator delete(void*, unsigned int)
#endif
    { "_ZSt7nothrow", (void*)&std::nothrow },
    DEFINE_MODULE_SYMBOL(__cxa_pure_virtual), // class-related, see https://arobenko.github.io/bare_metal_cpp/
    DEFINE_MODULE_SYMBOL(__cxa_guard_acquire),
    DEFINE_MODULE_SYMBOL(__cxa_guard_release),
    DEFINE_MODULE_SYMBOL(__cxa_guard_abort),
#ifdef ESP_PLATFORM
    DEFINE_MODULE_SYMBOL(__cxa_guard_dummy),
#endif
    // stl - Note: You have to use the mangled names here
    { "_ZSt17__throw_bad_allocv", (void*)&(std::__throw_bad_alloc) },
    { "_ZSt28__throw_bad_array_new_lengthv", (void*)&(std::__throw_bad_array_new_length) },
    { "_ZSt25__throw_bad_function_callv", (void*)&(std::__throw_bad_function_call) },
    { "_ZSt20__throw_length_errorPKc", (void*)&(std::__throw_length_error) },
    { "_ZSt19__throw_logic_errorPKc", (void*)&std::__throw_logic_error },
    { "_ZSt24__throw_out_of_range_fmtPKcz", (void*)&std::__throw_out_of_range_fmt },
    { "_ZSt20__throw_system_errori", (void*)&std::__throw_system_error },
    // stl - std::map / std::set (red-black tree internals)
    DEFINE_MODULE_SYMBOL(_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base),
    DEFINE_MODULE_SYMBOL(_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base),
    DEFINE_MODULE_SYMBOL(_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_),
#ifdef ESP_PLATFORM
    // string - Note: You have to use the mangled names here
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE15_M_replace_coldEPcjPKcjj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6substrEjj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_createERjj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7reserveEj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_disposeEv),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE10_M_replaceEjjPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructIPKcEEvT_S8_St20forward_iterator_tag),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEPKc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6assignEPKc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7_S_copyEPcPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7_S_moveEPcPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8_M_eraseEjj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8pop_backEv),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_appendEPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_assignERKS4_),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_mutateEjjPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9push_backEc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEaSEOS4_),
    // C1/C2/C5: complete-object, base-object, and comdat-folded aliases of the same constructor.
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC1ERKS4_jj", (void*)&construct_basic_string_from_substring },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2ERKS4_jj", (void*)&construct_basic_string_from_substring },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC5ERKS4_jj", (void*)&construct_basic_string_from_substring },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC1IS3_EEPKcRKS3_", (void*)&construct_basic_string_from_cstr },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2IS3_EEPKcRKS3_", (void*)&construct_basic_string_from_cstr },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC5IS3_EEPKcRKS3_", (void*)&construct_basic_string_from_cstr },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC1EOS4_", (void*)&construct_basic_string_move },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2EOS4_", (void*)&construct_basic_string_move },
    { "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC5EOS4_", (void*)&construct_basic_string_move },
    DEFINE_MODULE_SYMBOL(_ZSt12__str_concatINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEET_PKNS6_10value_typeENS6_9size_typeES9_SA_RKNS6_14allocator_typeE),
    DEFINE_MODULE_SYMBOL(_ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EERKS8_PKS5_),
#endif
#endif // TT_CPP_SYMBOLS_AVAILABLE
    MODULE_SYMBOL_TERMINATOR
};

extern "C" {

Module cpp_symbols_module = {
    .name = "cpp-symbols",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
