// SPDX-License-Identifier: Apache-2.0
#include <cpp_symbols/module.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    extern void* _Znaj(uint32_t size); // operator new[](unsigned int)
    extern void _ZdaPvj(void* p, uint64_t size); // operator delete[](void*, unsigned int)
    // Unsized forms: the compiler picks these over the sized ones above depending on context
    // (e.g. trivially-destructible types needing no array cookie), so both must be exported.
    extern void _ZdlPv(void* p); // operator delete(void*)
    extern void _ZdaPv(void* p); // operator delete[](void*)
#endif
    // cxx_guards.cpp
    // Itanium ABI's __guard type is a 64-bit int (only the first byte is the actual init flag;
    // the rest is implementation-defined, used here by libstdc++ for a fast-path atomic check).
    // Pulling in <memory> (for std::shared_ptr, added below) drags in bits/atomic_wait.h, which
    // declares these three with this exact `long long*` signature - a `void*` declaration used to
    // work because nothing else in this TU forced that header's declaration into scope, but the
    // two must now match exactly or GCC treats them as conflicting redeclarations.
    extern int __cxa_guard_acquire(long long* pg);
    extern void __cxa_guard_release(long long* pg) throw();
    extern void __cxa_guard_abort(long long* pg) throw();
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
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEPKcj(const void*, const char*, unsigned int);
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEPKcjj(const void*, const char*, unsigned int, unsigned int);
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5rfindEPKcjj(const void*, const char*, unsigned int, unsigned int);
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5rfindEcj(const void*, char, unsigned int);
    int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7compareERKS4_(const void*, const void*);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4swapERS4_(void*, void*);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6insertEjPKc(void*, unsigned int, const char*);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7replaceEjjPKcj(void*, unsigned int, unsigned int, const char*, unsigned int);
    unsigned int _ZNSt8__detail14__to_chars_lenIjEEjT_i(unsigned int, int);
    void _ZNSt8__detail18__to_chars_10_implIjEEvPcjT_(char*, unsigned int, unsigned int);
    // `unsigned long` overloads of the same two helpers - to_string(long)'s internal path,
    // distinct mangled names from the `unsigned int` ones above (to_string(unsigned)'s path).
    unsigned int _ZNSt8__detail14__to_chars_lenImEEjT_i(unsigned long, int);
    void _ZNSt8__detail18__to_chars_10_implImEEvPcjT_(char*, unsigned int, unsigned long);
    bool _ZSteqIcSt11char_traitsIcESaIcEEbRKNSt7__cxx1112basic_stringIT_T0_T1_EEPKS5_(const void*, const char*); // operator==(string const&, const char*)
    bool _ZSteqIcSt11char_traitsIcESaIcEEbRKNSt7__cxx1112basic_stringIT_T0_T1_EESA_(const void*, const void*); // operator==(string const&, string const&)
    // operator+ overloads: hidden return-value pointer (return basic_string by value).
    void* _ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EEOS8_S9_(void*, void*, void*);
    void* _ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EEPKS5_RKS8_(void*, const char*, const void*);
    void* _ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EERKS8_SA_(void*, const void*, const void*);
    // vector<unsigned char> and vector<std::string> internals - Note: mangled names required.
    void _ZNKSt6vectorIhSaIhEE12_M_check_lenEjPKc(const void*, unsigned int, const char*);
    void _ZNKSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE12_M_check_lenEjPKc(const void*, unsigned int, const char*);
    bool _ZNKSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE5emptyEv(const void*);
    void* _ZNSt12_Vector_baseIhSaIhEE11_M_allocateEj(void*, unsigned int);
    void* _ZNSt12_Vector_baseIhSaIhEE17_M_create_storageEj(void*, unsigned int);
    void _ZNSt6vectorIhSaIhEE17_M_default_appendEj(void*, unsigned int);
    void _ZNSt6vectorIhSaIhEE6resizeEj(void*, unsigned int);
    unsigned char* _ZNSt27__uninitialized_default_n_1ILb1EE18__uninit_default_nIPhjEET_S3_T0_(unsigned char*, unsigned int);
    void* _ZSt9__fill_a1IhhEN9__gnu_cxx11__enable_ifIXaasrSt9__is_byteIT_E7__valueoosrSt10__are_sameIS3_T0_E7__valuesrSt20__memcpyable_integerIS6_E7__widthEvE6__typeEPS3_SC_RKS6_(unsigned char*, unsigned char*, const unsigned char*);
    // std::mutex
    void _ZNSt5mutex4lockEv(void*);
    // Remaining basic_string/vector template instantiations, raw-extern like _M_construct's
    // forward-iterator overload above - these are genuinely out-of-line template
    // instantiations (not compiler-inlined ctors/dtors), so no forcing wrapper is needed.
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructILb1EEEvPKcj(void*, const char*, unsigned int);
    // resize_and_overwrite's instantiation is scoped to to_string(unsigned)'s own private lambda
    // type - unnameable directly, but calling std::to_string(unsigned) below (construct_
    // to_string_result) instantiates it in this TU under this exact mangled name, addressable
    // by name even though its C++ type can't be spelled outside to_string's own body.
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE20resize_and_overwriteIRZNS_9to_stringEjEUlPcjE_EEvjT_(void*, unsigned int, void*);
    void* _ZSt14__relocate_a_1IPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_SaIS5_EET0_T_S9_S8_RT1_(void*, void*, void*, void*);
    int _ZStssIcSt11char_traitsIcESaIcEEDTcl21__char_traits_cmp_catIT0_ELi0EEERKNSt7__cxx1112basic_stringIT_S3_T1_EESB_(const void*, const void*); // operator<=>(string const&, string const&)

    long _ZNKSt8functionIFlvEEclEv(const void*); // std::function<long()>::operator()() const
    void _ZNSt14_Function_baseD2Ev(void*); // std::_Function_base::~_Function_base()
    void _ZNSt8functionIFlvEEC1ERKS1_(void*, const void*); // std::function<long()>::function(function const&)

    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE13find_first_ofEPKcj(const void*, const char*, unsigned int);
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE13find_first_ofEPKcjj(const void*, const char*, unsigned int, unsigned int);
    unsigned int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4copyEPcjj(const void*, char*, unsigned int, unsigned int);
    int _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7compareEjjPKc(const void*, unsigned int, unsigned int, const char*);
    void _ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8_M_checkEjPKc(const void*, unsigned int, const char*);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructEjc(void*, unsigned int, char);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE14_M_replace_auxEjjjc(void*, unsigned int, unsigned int, unsigned int, char);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5eraseEjj(void*, unsigned int, unsigned int);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEjc(void*, unsigned int, char);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6assignEPKcj(void*, const char*, unsigned int);
    void* _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEjc(void*, unsigned int, char);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_S_assignEPcjc(char*, unsigned int, char);
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC1EPKcjRKS3_(void*, const char*, unsigned int, const void*);
    // resize_and_overwrite/to_string's `long` overloads - distinct mangled names from the
    // `unsigned int` ones already handled above (construct_to_string_result et al.).
    void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE20resize_and_overwriteIRZNS_9to_stringElEUlPcjE_EEvjT_(void*, unsigned int, void*);

    // std::_Rb_tree<std::string, std::pair<const std::string, std::string>, ...> - std::map<string,
    // string>'s internals.
    void* _ZNKSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_lower_boundEPSt18_Rb_tree_node_baseSG_RS7_(const void*, void*, void*, const void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE10_Auto_node9_M_insertES6_IPSt18_Rb_tree_node_baseSH_E(void*, void*, void*);
    void _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE10_Auto_nodeD1Ev(void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE11lower_boundERS7_(void*, const void*);
    void _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE12_M_drop_nodeEPSt13_Rb_tree_nodeIS8_E(void*, void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_create_nodeIJRKSt21piecewise_construct_tSt5tupleIJOS5_EESJ_IJEEEEEPSt13_Rb_tree_nodeIS8_EDpOT_(void*, const void*, void*, void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_create_nodeIJRKSt21piecewise_construct_tSt5tupleIJRS7_EESJ_IJEEEEEPSt13_Rb_tree_nodeIS8_EDpOT_(void*, const void*, void*, void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_insert_nodeEPSt18_Rb_tree_node_baseSG_PSt13_Rb_tree_nodeIS8_E(void*, void*, void*, void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE22_M_emplace_hint_uniqueIJRKSt21piecewise_construct_tSt5tupleIJOS5_EESJ_IJEEEEESt17_Rb_tree_iteratorIS8_ESt23_Rb_tree_const_iteratorIS8_EDpOT_(void*, void*, const void*, void*, void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE22_M_emplace_hint_uniqueIJRKSt21piecewise_construct_tSt5tupleIJRS7_EESJ_IJEEEEESt17_Rb_tree_iteratorIS8_ESt23_Rb_tree_const_iteratorIS8_EDpOT_(void*, void*, const void*, void*, void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE24_M_get_insert_unique_posERS7_(void*, const void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE29_M_get_insert_hint_unique_posESt23_Rb_tree_const_iteratorIS8_ERS7_(void*, void*, const void*);
    void* _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE4findERS7_(void*, const void*);
    void _ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE8_M_eraseEPSt13_Rb_tree_nodeIS8_E(void*, void*);

    // std::map<std::string, std::string>::operator[] (two overloads: rvalue key and const& key).
    void* _ZNSt3mapINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES5_St4lessIS5_ESaISt4pairIKS5_S5_EEEixEOS5_(void*, void*);
    void* _ZNSt3mapINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES5_St4lessIS5_ESaISt4pairIKS5_S5_EEEixERS9_(void*, const void*);
    // pair<const string, string>(piecewise_construct_t, tuple<string&>, tuple<>) - the map-node
    // in-place construction path std::map<string,string>::operator[] uses internally.
    void _ZNSt4pairIKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES5_EC1IJRS6_EJEEESt21piecewise_construct_tSt5tupleIJDpT_EESB_IJDpT0_EE(void*, const void*, void*, void*);

    // Private helpers with no independently-callable name (can't take their address
    // directly - they're private members / SFINAE-constrained template overloads).
    // The forcing wrappers above (destroy_deque_char and friends) already make GCC
    // emit real out-of-line definitions for each of these as a side effect of calling
    // the public API; once emitted, a plain extern declaration resolves at link time
    // even though this TU can never call them by name itself.
    void _ZNSt11_Deque_baseIcSaIcEEC2Ev(void*);
    void _ZNSt11_Deque_baseIcSaIcEED2Ev(void*);
    void _ZNSt11_Deque_baseIcSaIcEE15_M_create_nodesEPPcS3_(void*, char**, char**);
    void _ZNSt11_Deque_baseIcSaIcEE16_M_destroy_nodesEPPcS3_(void*, char**, char**);
    void _ZNSt11_Deque_baseIcSaIcEE17_M_initialize_mapEj(void*, unsigned int);
    void _ZNSt11_Deque_baseIdSaIdEEC2Ev(void*);
    void _ZNSt11_Deque_baseIdSaIdEED2Ev(void*);
    void _ZNSt11_Deque_baseIdSaIdEE15_M_create_nodesEPPdS3_(void*, double**, double**);
    void _ZNSt11_Deque_baseIdSaIdEE16_M_destroy_nodesEPPdS3_(void*, double**, double**);
    void _ZNSt11_Deque_baseIdSaIdEE17_M_initialize_mapEj(void*, unsigned int);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EEC2Ev(void*);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EEC2EOS7_(void*, void*);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EED2Ev(void*);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE15_M_create_nodesEPPS5_S9_(void*, void*, void*);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_destroy_nodesEPPS5_S9_(void*, void*, void*);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_M_initialize_mapEj(void*, unsigned int);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_Deque_impl_dataC1EOS8_(void*, void*);
    void _ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_Deque_impl_dataC1ERKS8_(void*, const void*);
    void* _ZSt4swapINSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS6_EE16_Deque_impl_dataEENSt9enable_ifIXsrSt6__and_IJSt6__not_ISt15__is_tuple_likeIT_EESt21is_move_constructibleISE_ESt18is_move_assignableISE_EEE5valueEvE4typeERSE_SO_(void*, void*);
    void _ZNSt5dequeIcSaIcEE15_M_pop_back_auxEv(void*);
    void _ZNSt5dequeIcSaIcEE16_M_push_back_auxIJRKcEEEvDpOT_(void*, const char*);
    void _ZNSt5dequeIcSaIcEE16_M_push_back_auxIJcEEEvDpOT_(void*, char*);
    void _ZNSt5dequeIcSaIcEE17_M_reallocate_mapEjb(void*, unsigned int, bool);
    void _ZNSt5dequeIcSaIcEE22_M_reserve_map_at_backEj(void*, unsigned int);
    void _ZNSt5dequeIdSaIdEE15_M_pop_back_auxEv(void*);
    void _ZNSt5dequeIdSaIdEE16_M_push_back_auxIJRKdEEEvDpOT_(void*, const double*);
    void _ZNSt5dequeIdSaIdEE16_M_push_back_auxIJdEEEvDpOT_(void*, double*);
    void _ZNSt5dequeIdSaIdEE17_M_reallocate_mapEjb(void*, unsigned int, bool);
    void _ZNSt5dequeIdSaIdEE22_M_reserve_map_at_backEj(void*, unsigned int);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_pop_front_auxEv(void*);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_push_back_auxIJRKS5_EEEvDpOT_(void*, const void*);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_push_back_auxIJS5_EEEvDpOT_(void*, void*);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_M_reallocate_mapEjb(void*, unsigned int, bool);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE22_M_reserve_map_at_backEj(void*, unsigned int);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE15_M_destroy_dataESt15_Deque_iteratorIS5_RS5_PS5_ESB_RKS6_(void*, void*, void*, const void*);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE15_M_erase_at_endESt15_Deque_iteratorIS5_RS5_PS5_E(void*, void*);
    void _ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE19_M_destroy_data_auxESt15_Deque_iteratorIS5_RS5_PS5_ESB_(void*, void*, void*);
    void _ZNKSt6vectorIPKcSaIS1_EE12_M_check_lenEjS1_(const void*, unsigned int, const char*);
    void _ZNKSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE12_M_check_lenEjPKc(const void*, unsigned int, const char*);
    void _ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE15_M_erase_at_endEPS7_(void*, void*);
    void* _ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE4backEv(void*);
    void _ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE10_M_destroyEv(void*);
    void _ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE10_M_releaseEv(void*);
    void _ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE19_M_release_last_useEv(void*);
    void _ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE10_M_destroyEv(void*);
    void _ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE10_M_releaseEv(void*);
    void _ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE19_M_release_last_useEv(void*);
    // Vtable for the abstract _Sp_counted_base<Lock_policy> itself (not a derived
    // _Sp_counted_ptr<T,...>'s vtable, which does vary per pointee type) - a vtable has no valid
    // C++ spelling, but a `V` (weak vague-linkage) symbol like this one is a real extern-able
    // linker symbol once something forces its emission (see the shared_count forcing wrappers).
    extern void* _ZTVSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE;
    extern void* _ZTVSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE;
#endif
}

namespace {
// __cxa_pure_virtual used to be a plain `extern` reference to libstdc++.a(pure.o)'s definition
// (just `entry; l32r; callx8` to std::terminate() - confirmed by disassembling pure.o directly).
// That stopped linking on ESP_PLATFORM once this file grew enough new libstdc++ surface area
// (deque/vector<pair>/shared_ptr/function below) to transitively pull in locale/codecvt
// machinery: some of those archive members (e.g. lt1-codecvt.o, for their own pure-virtual
// destructor scaffolding) carry their own weak-undefined reference to the same name, and
// something in how GNU ld scans this larger set of archives now resolves the whole program's
// __cxa_pure_virtual lookup against one of those weak-undefined placeholders instead of ever
// pulling pure.o's real definition in - silently leaving the exported function pointer null (no
// link error, since a weak-undefined reference is optional; confirmed with `nm`/the link map -
// pure.o never appears in the final link at all once this happens). Rather than fight
// archive-scan ordering, this reimplements the three-instruction real behavior directly so it no
// longer depends on pulling in pure.o at all. Deliberately NOT gated behind #ifdef ESP_PLATFORM
// (unlike the rest of this anonymous namespace) - the original plain `extern` this replaces was
// unconditional (also exported on the POSIX/simulator build, which needs its own __cxa_pure_virtual
// too), and std::terminate() is available on both.
[[noreturn]] void pure_virtual_called() {
    std::terminate();
}
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

// vector<std::string>: same story as basic_string's ctors above - the destructor for a
// non-trivial element type has no out-of-line definition anywhere to take the address of, so a
// wrapper that actually destroys one forces the compiler to emit a genuine, addressable
// definition here, registered below under the mangled name the ELF loader looks up.
void destroy_vector_of_strings(void* self) {
    static_cast<std::vector<std::string>*>(self)->~vector();
}
// Same story for __new_allocator<std::string>::allocate() (used internally by vector<string>'s
// growth/reserve path).
void* allocate_string_storage(void* self, unsigned int n, const void* hint) {
    return static_cast<std::__new_allocator<std::string>*>(self)->allocate(n, hint);
}

// Same story for vector<unsigned char>'s destructor.
void destroy_vector_of_bytes(void* self) {
    static_cast<std::vector<unsigned char>*>(self)->~vector();
}

// _Vector_base<T>::~_Vector_base only deallocates the raw buffer (element destruction is
// vector<T>::~vector()'s job, called before this) - a distinct entry point with no accessible
// out-of-line definition of its own (it's a protected base of vector<T>, and its destructor body
// is a one-line inline in the header). A shim that publicly re-derives from it regains access to
// call the real destructor at the right address, without duplicating its (private) cleanup logic.
struct StringVectorBaseShim : std::_Vector_base<std::string, std::allocator<std::string>> {};
void destroy_vector_base_of_strings(void* self) {
    static_cast<StringVectorBaseShim*>(self)->~StringVectorBaseShim();
}

// std::to_string(unsigned) is `inline` in the header (no prebuilt out-of-line definition in
// libstdc++'s archive) - the app calls it directly as an opaque function in at least one call
// site (rather than always inlining it), so it needs a real out-of-line definition under its own
// mangled name, not just a forcing side effect. Hidden return-value pointer convention, matching
// the other string-returning functions (operator+, __str_concat) elsewhere in this file. This
// same call also instantiates resize_and_overwrite()'s to_string-private lambda, registered
// separately below under its own mangled name.
void construct_to_string_result(void* out, unsigned int value) {
    new (out) std::string(std::to_string(value));
}
// Same story for std::to_string(long) - a distinct overload/mangled name from the unsigned one
// above. Also instantiates its own private resize_and_overwrite lambda, registered separately
// under its own mangled name (see the extern declarations above).
void construct_to_string_result_long(void* out, long value) {
    new (out) std::string(std::to_string(value));
}

// basic_string::rfind<string_view>(string_view const&, pos) is a small header-inline SFINAE
// forwarder around the already-exported rfind(const char*, pos, n) overload - no prebuilt
// out-of-line definition either, same story as to_string above.
[[gnu::used]] unsigned int rfind_string_view(const std::string& self, const std::string_view& sv, unsigned int pos) {
    return self.rfind(sv, pos);
}

// _Vector_base<T>::_Vector_impl_data::_M_swap_data - a plain one-line inline swap of three
// pointers, normally never emitted out-of-line, but the app references it directly anyway
// (matches _M_replace_cold's "kept out of line" pattern elsewhere in this file). Its type and
// method are public members of the (struct, so default-public) _Vector_base.
using StringVectorImplData = std::_Vector_base<std::string, std::allocator<std::string>>::_Vector_impl_data;
void swap_string_vector_impl_data(void* self, void* other) {
    static_cast<StringVectorImplData*>(self)->_M_swap_data(*static_cast<StringVectorImplData*>(other));
}
// Same story for vector<unsigned char>(size_type, allocator const&).
void construct_vector_of_bytes_sized(void* self, unsigned int count, const void* alloc) {
    new (self) std::vector<unsigned char>(count, *static_cast<const std::allocator<unsigned char>*>(alloc));
}

// Same _Vector_base<T>::~_Vector_base distinction as the string version above, for
// vector<unsigned char>.
struct ByteVectorBaseShim : std::_Vector_base<unsigned char, std::allocator<unsigned char>> {};
void destroy_vector_base_of_bytes(void* self) {
    static_cast<ByteVectorBaseShim*>(self)->~ByteVectorBaseShim();
}

// push_back/emplace_back/back on vector<string> are ordinary public member functions with
// out-of-line definitions, registered below under their real mangled names - the app's own
// push_back/emplace_back calls resolve directly here, so each must insert exactly once.
void push_back_string(void* self, const std::string& value) {
    static_cast<std::vector<std::string>*>(self)->push_back(value);
}
std::string& emplace_back_string(void* self, std::string&& value) {
    return static_cast<std::vector<std::string>*>(self)->emplace_back(std::move(value));
}

// _M_realloc_append(T&&) is what push_back/emplace_back call internally when the vector needs
// to grow - the app can also call it directly (its own compiled code inlined the fast/slow-path
// split from push_back's body, keeping only the growth call out-of-line). These do exactly what
// _M_realloc_append itself does (append one element, growing the vector), so redirecting the
// app's call here is behavior-preserving - unlike push_back_string/emplace_back_string above,
// which append TWO elements and must not be reused for this.
void realloc_append_string_const_ref(void* self, const std::string& value) {
    static_cast<std::vector<std::string>*>(self)->push_back(value);
}
void realloc_append_string_rvalue(void* self, std::string&& value) {
    static_cast<std::vector<std::string>*>(self)->push_back(std::move(value));
}

// std::deque<char>/<double>/<basic_string<char>> - unlike basic_string, libstdc++ does NOT
// `extern template` instantiate std::deque anywhere, so NONE of its members (public or private)
// exist as prebuilt symbols in libstdc++.a - a plain extern declaration link-fails even though it
// compiles. Every wrapper below calls only the PUBLIC deque API on a real object; that forces the
// compiler to instantiate and emit genuine out-of-line definitions for both the public entry point
// and whichever private _M_*/_Deque_base helpers it needs internally, each addressable under its
// own real mangled name (registered directly in SYMBOLS[] below, same as _M_realloc_append above).
void destroy_deque_char(void* self) { static_cast<std::deque<char>*>(self)->~deque(); }
char& back_of_deque_char(void* self) { return static_cast<std::deque<char>*>(self)->back(); }
void pop_back_deque_char(void* self) { static_cast<std::deque<char>*>(self)->pop_back(); }
void push_back_deque_char(void* self, const char& v) { static_cast<std::deque<char>*>(self)->push_back(v); }
char& emplace_back_deque_char(void* self, char&& v) { return static_cast<std::deque<char>*>(self)->emplace_back(std::move(v)); }

void destroy_deque_double(void* self) { static_cast<std::deque<double>*>(self)->~deque(); }
double& back_of_deque_double(void* self) { return static_cast<std::deque<double>*>(self)->back(); }
void pop_back_deque_double(void* self) { static_cast<std::deque<double>*>(self)->pop_back(); }
void push_back_deque_double(void* self, const double& v) { static_cast<std::deque<double>*>(self)->push_back(v); }
double& emplace_back_deque_double(void* self, double&& v) { return static_cast<std::deque<double>*>(self)->emplace_back(std::move(v)); }

using StringDeque = std::deque<std::string>;
void construct_deque_string(void* self) { new (self) StringDeque(); }
void move_construct_deque_string(void* self, void* other) { new (self) StringDeque(std::move(*static_cast<StringDeque*>(other))); }
void destroy_deque_string(void* self) { static_cast<StringDeque*>(self)->~deque(); }
std::string& back_of_deque_string(void* self) { return static_cast<StringDeque*>(self)->back(); }
void clear_deque_string(void* self) { static_cast<StringDeque*>(self)->clear(); }
void pop_front_deque_string(void* self) { static_cast<StringDeque*>(self)->pop_front(); }
void push_back_deque_string(void* self, const std::string& v) { static_cast<StringDeque*>(self)->push_back(v); }
std::string& emplace_back_deque_string(void* self, std::string&& v) { return static_cast<StringDeque*>(self)->emplace_back(std::move(v)); }

// std::stack<T, deque<T>>'s default constructor - trivial, but (like everything else deque-related
// here) has no prebuilt out-of-line definition anywhere; a value-initializing placement-new forces
// the compiler to emit one.
void construct_stack_char(void* self) { new (self) std::stack<char, std::deque<char>>(); }
void construct_stack_double(void* self) { new (self) std::stack<double, std::deque<double>>(); }

// std::_Deque_iterator<T,...>::operator--() and operator-(iterator, iterator) - reached through
// std::stack<T>::pop()/size() internals when T's deque grows past a single node. Forced the same
// way: call the real operators on real iterators (obtained via begin()/end(), which are cheap and
// always valid even on an empty deque).
void decrement_deque_iterator_char(void* self) { --*static_cast<std::deque<char>::iterator*>(self); }
void decrement_deque_iterator_double(void* self) { --*static_cast<std::deque<double>::iterator*>(self); }
void decrement_deque_iterator_string(void* self) { --*static_cast<StringDeque::iterator*>(self); }
int subtract_deque_iterators_char(const void* a, const void* b) {
    return static_cast<int>(*static_cast<const std::deque<char>::iterator*>(a) - *static_cast<const std::deque<char>::iterator*>(b));
}
int subtract_deque_iterators_double(const void* a, const void* b) {
    return static_cast<int>(*static_cast<const std::deque<double>::iterator*>(a) - *static_cast<const std::deque<double>::iterator*>(b));
}
int subtract_deque_iterators_string(const void* a, const void* b) {
    return static_cast<int>(*static_cast<const StringDeque::iterator*>(a) - *static_cast<const StringDeque::iterator*>(b));
}

// std::vector<const char*> and std::vector<std::pair<std::string, bool>> - same "no extern
// template anywhere" story as deque above (nothing else in the firmware happens to instantiate
// these two particular element types, unlike vector<string>/vector<unsigned char> which do get
// pulled in elsewhere and so link fine as plain externs). Forced via the public API, same pattern.
using CStrVector = std::vector<const char*>;
void destroy_vector_base_of_cstrs(void* self) { static_cast<std::_Vector_base<const char*, std::allocator<const char*>>*>(self)->~_Vector_base(); }
const char*& back_of_cstr_vector(void* self) { return static_cast<CStrVector*>(self)->back(); }
const char*& emplace_back_cstr_vector(void* self, const char*&& v) { return static_cast<CStrVector*>(self)->emplace_back(std::move(v)); }
void realloc_append_cstr_vector(void* self, const char*&& v) { static_cast<CStrVector*>(self)->push_back(std::move(v)); }
void reserve_cstr_vector(void* self, unsigned int n) { static_cast<CStrVector*>(self)->reserve(n); }
void* allocate_cstr_storage(void* self, unsigned int n, const void* hint) {
    return static_cast<std::__new_allocator<const char*>*>(self)->allocate(n, hint);
}

using StringBoolPair = std::pair<std::string, bool>;
using StringBoolPairVector = std::vector<StringBoolPair>;
bool empty_string_bool_pair_vector(const void* self) { return static_cast<const StringBoolPairVector*>(self)->empty(); }
// erase(begin(), end()) calls _M_erase_at_end(pointer) internally, forcing its instantiation
// under the mangled name registered directly via DEFINE_MODULE_SYMBOL below - this function
// itself is never called or registered, it exists purely so that call happens somewhere in this
// TU. [[gnu::used]] keeps the compiler from dead-stripping it as an uncalled anonymous-namespace
// function, which would silently drop the instantiation it exists to force.
[[gnu::used]] void clear_string_bool_pair_vector(void* self) {
    auto* v = static_cast<StringBoolPairVector*>(self);
    v->erase(v->begin(), v->end());
}
void destroy_vector_base_of_string_bool_pairs(void* self) { static_cast<std::_Vector_base<StringBoolPair, std::allocator<StringBoolPair>>*>(self)->~_Vector_base(); }
void destroy_vector_of_string_bool_pairs(void* self) { static_cast<StringBoolPairVector*>(self)->~vector(); }
StringBoolPair& emplace_back_string_bool_pair_vector(void* self, StringBoolPair&& v) { return static_cast<StringBoolPairVector*>(self)->emplace_back(std::move(v)); }
void realloc_append_string_bool_pair_vector(void* self, StringBoolPair&& v) { static_cast<StringBoolPairVector*>(self)->push_back(std::move(v)); }
void* allocate_string_bool_pair_storage(void* self, unsigned int n, const void* hint) {
    return static_cast<std::__new_allocator<StringBoolPair>*>(self)->allocate(n, hint);
}
void* allocate_string_ptr_storage(void* self, unsigned int n, const void* hint) {
    return static_cast<std::__new_allocator<std::string*>*>(self)->allocate(n, hint);
}
void* allocate_char_ptr_storage(void* self, unsigned int n, const void* hint) {
    return static_cast<std::__new_allocator<char*>*>(self)->allocate(n, hint);
}
void* allocate_double_ptr_storage(void* self, unsigned int n, const void* hint) {
    return static_cast<std::__new_allocator<double*>*>(self)->allocate(n, hint);
}

// std::__shared_count<Lp>/_Sp_counted_base<Lp> - no extern-template instantiation anywhere, same
// story as deque/vector<pair<...>> above. Forced via std::shared_ptr<int> rather than a real app
// type: the raw-pointer constructor's mangled name embeds the pointee type, but operator= and
// _Sp_counted_base's virtual dispatch (forced as a side effect of constructing one) don't - their
// mangled names carry only the Lock_policy, so those two are genuinely reusable across every app's
// shared_ptr<AnyType> regardless of what int has to do with any of them.
using GenericSharedCountAtomic = std::__shared_count<__gnu_cxx::_Lock_policy::_S_atomic>;
using GenericSharedCountMutex = std::__shared_count<__gnu_cxx::_Lock_policy::_S_mutex>;
// [[gnu::used]]: neither of these is registered in SYMBOLS[] under its own name (nothing looks
// up "construct a shared_ptr<int>" by that description) - they exist purely so the compiler
// instantiates _Sp_counted_base<Lock_policy>'s virtual table and out-of-line thunks as a side
// effect, which -ffunction-sections/--gc-sections would otherwise strip as dead code since no
// other code in this TU calls them and nothing takes their address.
[[gnu::used]] void construct_shared_count_atomic_int(void* self, int* p) { new (self) GenericSharedCountAtomic(p); }
void assign_shared_count_atomic(void* self, const void* other) {
    *static_cast<GenericSharedCountAtomic*>(self) = *static_cast<const GenericSharedCountAtomic*>(other);
}
[[gnu::used]] void construct_shared_count_mutex_int(void* self, int* p) { new (self) GenericSharedCountMutex(p); }
void assign_shared_count_mutex(void* self, const void* other) {
    *static_cast<GenericSharedCountMutex*>(self) = *static_cast<const GenericSharedCountMutex*>(other);
}
// _Sp_counted_base<Lock_policy> is an abstract base (_M_destroy/_M_release/_M_release_last_use
// are virtual, overridden by the concrete _Sp_counted_ptr<T*,...> that __shared_count's
// constructor above actually allocates) - can't instantiate it standalone, but constructing a real
// GenericSharedCount*(ptr) above already forces the compiler to emit its base class's virtual
// table and out-of-line virtual function thunks, addressable under _Sp_counted_base's own mangled
// names because that's the base subobject type the virtual calls are dispatched through, and
// those names carry only the Lock_policy (not the pointee type T), so this generic forcing works
// for every app's shared_ptr<AnyType> without needing to know AnyType.
//
// The raw-pointer __shared_count<Lp>::__shared_count<T*>(T*) constructor itself is deliberately
// NOT forced/exported here even though its mangled name does embed a pointee type T: the app's
// own compiler already emits a local, weak-linkage definition of that exact constructor in the
// app's own ELF whenever it does `shared_ptr<T>(new T(...))`, and the loader falls back to an
// app-local definition when the firmware doesn't provide one (esp_elf.c's relocate loop: tries
// the firmware's exported-symbol table first, falls back to the symbol's own local address when
// st_shndx != SHN_UNDEF) - so nothing needs to be added here for it to work.

// std::function<int(char*, unsigned int)> - no extern-template instantiation anywhere, so forced
// via the public API on a real object, same pattern as everything else above.
using CharBufferFn = std::function<int(char*, unsigned int)>;
int call_char_buffer_fn(const void* self, char* buf, unsigned int len) { return (*static_cast<const CharBufferFn*>(self))(buf, len); }
void swap_char_buffer_fn(void* self, void* other) { static_cast<CharBufferFn*>(self)->swap(*static_cast<CharBufferFn*>(other)); }
void construct_char_buffer_fn_copy(void* self, const void* other) { new (self) CharBufferFn(*static_cast<const CharBufferFn*>(other)); }
void* assign_char_buffer_fn_nullptr(void* self) { return &(*static_cast<CharBufferFn*>(self) = nullptr); }

// _Guard_alloc is a private RAII rollback guard nested inside vector<T>::_M_realloc_append's own
// body (rolls back the new buffer if appending throws mid-copy) - not derivable-into like
// _Vector_base above, since it's private to vector<T> itself rather than a protected base. Its
// layout is fixed by the header ({pointer storage, size_type len, _Base& vect}) and its destructor
// body is just "deallocate storage via the vector's allocator if non-null", so this replicates
// that logic directly against the same layout instead of calling the inaccessible real dtor.
template <typename T>
struct GuardAllocLayout {
    T* storage;
    std::size_t len;
    void* vect;
};
template <typename T>
void destroy_guard_alloc(void* self) {
    auto* guard = static_cast<GuardAllocLayout<T>*>(self);
    if (guard->storage) {
        std::allocator<T>().deallocate(guard->storage, guard->len);
    }
}
std::string& back_of_string_vector(void* self) {
    return static_cast<std::vector<std::string>*>(self)->back();
}
// vector<string>'s move-assignment operator (self = std::move(other)) - forces the private
// _M_move_assign/_M_swap_data helpers it needs to be emitted as addressable definitions too.
void move_assign_string_vector(void* self, void* other) {
    *static_cast<std::vector<std::string>*>(self) = std::move(*static_cast<std::vector<std::string>*>(other));
}

// Misc algorithm/iterator template instantiations pulled in by std::stack<char/double> and
// vector<pair<string,bool>> usage - none prebuilt anywhere, forced by calling the public std::
// algorithm entry point on real pointers/iterators of the exact instantiated type.
std::string** copy_move_a2_string_ptr(std::string** first, std::string** last, std::string** out) {
    return std::copy(first, last, out);
}
char** copy_move_a2_char_ptr(char** first, char** last, char** out) { return std::copy(first, last, out); }
double** copy_move_a2_double_ptr(double** first, double** last, double** out) { return std::copy(first, last, out); }
StringBoolPair* relocate_string_bool_pairs(StringBoolPair* first, StringBoolPair* last, StringBoolPair* out, std::allocator<StringBoolPair>& alloc) {
    return std::__relocate_a(first, last, out, alloc);
}
std::string** copy_move_backward_a2_string_ptr(std::string** first, std::string** last, std::string** out) {
    return std::copy_backward(first, last, out);
}
char** copy_move_backward_a2_char_ptr(char** first, char** last, char** out) { return std::copy_backward(first, last, out); }
double** copy_move_backward_a2_double_ptr(double** first, double** last, double** out) { return std::copy_backward(first, last, out); }
StringBoolPair* move_backward_string_bool_pairs(StringBoolPair* first, StringBoolPair* last, StringBoolPair* out) {
    return std::move_backward(first, last, out);
}
short max_short_init_list(std::initializer_list<short> values) {
    return std::max(values);
}
short min_short_init_list(std::initializer_list<short> values) {
    return std::min(values);
}
void advance_string_ptr_ptr(std::string*** it, int n) { std::advance(*it, n); }
void advance_char_ptr_ptr(char*** it, int n) { std::advance(*it, n); }
void advance_double_ptr_ptr(double*** it, int n) { std::advance(*it, n); }
void iter_swap_string_bool_pair_vector(StringBoolPairVector::iterator a, StringBoolPairVector::iterator b) {
    std::iter_swap(a, b);
}
__gnu_cxx::__normal_iterator<char*, std::string> transform_char_to_upper(
    __gnu_cxx::__normal_iterator<char*, std::string> first,
    __gnu_cxx::__normal_iterator<char*, std::string> last,
    __gnu_cxx::__normal_iterator<char*, std::string> out,
    int (*fn)(int)
) {
    return std::transform(first, last, out, fn);
}
// std::_Any_data (std::function's internal storage union) - swap<T> is SFINAE-enabled only for
// move-constructible/assignable, non-tuple-like T; _Any_data qualifies, so calling std::swap on
// two real ones forces this exact overload's instantiation.
void swap_any_data(void* a, void* b) {
    std::swap(*static_cast<std::_Any_data*>(a), *static_cast<std::_Any_data*>(b));
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
    DEFINE_MODULE_SYMBOL(_Znaj), // operator new[](unsigned int)
    DEFINE_MODULE_SYMBOL(_ZdaPvj), // operator delete[](void*, unsigned int)
    DEFINE_MODULE_SYMBOL(_ZdlPv), // operator delete(void*)
    DEFINE_MODULE_SYMBOL(_ZdaPv), // operator delete[](void*)
#endif
    { "_ZSt7nothrow", (void*)&std::nothrow },
    { "__cxa_pure_virtual", (void*)&pure_virtual_called }, // class-related, see https://arobenko.github.io/bare_metal_cpp/ - own impl, see comment above pure_virtual_called
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
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEPKcj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEPKcjj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5rfindEPKcjj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5rfindEcj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7compareERKS4_),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4swapERS4_),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6insertEjPKc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7replaceEjjPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt8__detail14__to_chars_lenIjEEjT_i),
    DEFINE_MODULE_SYMBOL(_ZNSt8__detail18__to_chars_10_implIjEEvPcjT_),
    DEFINE_MODULE_SYMBOL(_ZNSt8__detail14__to_chars_lenImEEjT_i),
    DEFINE_MODULE_SYMBOL(_ZNSt8__detail18__to_chars_10_implImEEvPcjT_),
    DEFINE_MODULE_SYMBOL(_ZSteqIcSt11char_traitsIcESaIcEEbRKNSt7__cxx1112basic_stringIT_T0_T1_EEPKS5_),
    DEFINE_MODULE_SYMBOL(_ZSteqIcSt11char_traitsIcESaIcEEbRKNSt7__cxx1112basic_stringIT_T0_T1_EESA_),
    DEFINE_MODULE_SYMBOL(_ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EEOS8_S9_),
    DEFINE_MODULE_SYMBOL(_ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EEPKS5_RKS8_),
    DEFINE_MODULE_SYMBOL(_ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIT_T0_T1_EERKS8_SA_),
    DEFINE_MODULE_SYMBOL(_ZNKSt6vectorIhSaIhEE12_M_check_lenEjPKc),
    DEFINE_MODULE_SYMBOL(_ZNKSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE12_M_check_lenEjPKc),
    DEFINE_MODULE_SYMBOL(_ZNKSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE5emptyEv),
    DEFINE_MODULE_SYMBOL(_ZNSt12_Vector_baseIhSaIhEE11_M_allocateEj),
    DEFINE_MODULE_SYMBOL(_ZNSt12_Vector_baseIhSaIhEE17_M_create_storageEj),
    DEFINE_MODULE_SYMBOL(_ZNSt6vectorIhSaIhEE17_M_default_appendEj),
    DEFINE_MODULE_SYMBOL(_ZNSt6vectorIhSaIhEE6resizeEj),
    DEFINE_MODULE_SYMBOL(_ZNSt27__uninitialized_default_n_1ILb1EE18__uninit_default_nIPhjEET_S3_T0_),
    DEFINE_MODULE_SYMBOL(_ZSt9__fill_a1IhhEN9__gnu_cxx11__enable_ifIXaasrSt9__is_byteIT_E7__valueoosrSt10__are_sameIS3_T0_E7__valuesrSt20__memcpyable_integerIS6_E7__widthEvE6__typeEPS3_SC_RKS6_),
    DEFINE_MODULE_SYMBOL(_ZNSt5mutex4lockEv),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructILb1EEEvPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE20resize_and_overwriteIRZNS_9to_stringEjEUlPcjE_EEvjT_),
    { "_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5rfindISt17basic_string_viewIcS2_EEENSt9enable_ifIXsrSt6__and_IJSt14is_convertibleIRKT_S7_ESt6__not_ISA_IPSC_PKS4_EESF_ISA_ISD_PKcEEEE5valueEjE4typeESD_j", (void*)&rfind_string_view },
    DEFINE_MODULE_SYMBOL(_ZSt14__relocate_a_1IPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_SaIS5_EET0_T_S9_S8_RT1_),
    DEFINE_MODULE_SYMBOL(_ZStssIcSt11char_traitsIcESaIcEEDTcl21__char_traits_cmp_catIT0_ELi0EEERKNSt7__cxx1112basic_stringIT_S3_T1_EESB_),
    // vector<std::string> - Note: You have to use the mangled names here
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EED1Ev", (void*)&destroy_vector_of_strings },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EED2Ev", (void*)&destroy_vector_of_strings },
    { "_ZNSt15__new_allocatorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEE8allocateEjPKv", (void*)&allocate_string_storage },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE9push_backERKS5_", (void*)&push_back_string },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE12emplace_backIJS5_EEERS5_DpOT_", (void*)&emplace_back_string },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE4backEv", (void*)&back_of_string_vector },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE14_M_move_assignEOS7_St17integral_constantIbLb1EE", (void*)&move_assign_string_vector },
    // vector<unsigned char> - Note: You have to use the mangled names here
    { "_ZNSt6vectorIhSaIhEED1Ev", (void*)&destroy_vector_of_bytes },
    { "_ZNSt6vectorIhSaIhEED2Ev", (void*)&destroy_vector_of_bytes },
    { "_ZNSt6vectorIhSaIhEEC1EjRKS0_", (void*)&construct_vector_of_bytes_sized },
    { "_ZNSt12_Vector_baseIhSaIhEED2Ev", (void*)&destroy_vector_base_of_bytes },
    // _Vector_base<T>::~_Vector_base only deallocates the raw buffer (elements are destroyed by
    // vector<T>'s own destructor before this runs) - a distinct entry point from ~vector(), so
    // it needs its own wrapper rather than reusing destroy_vector_of_strings/_of_bytes above.
    { "_ZNSt12_Vector_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EED2Ev", (void*)&destroy_vector_base_of_strings },
    { "_ZNSt7__cxx119to_stringEj", (void*)&construct_to_string_result },
    { "_ZNSt12_Vector_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_Vector_impl_data12_M_swap_dataERS8_", (void*)&swap_string_vector_impl_data },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_M_realloc_appendIJRKS5_EEEvDpOT_", (void*)&realloc_append_string_const_ref },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_M_realloc_appendIJS5_EEEvDpOT_", (void*)&realloc_append_string_rvalue },
    { "_ZNSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE12_Guard_allocD1Ev", (void*)&destroy_guard_alloc<std::string> },
    { "_ZNSt6vectorIhSaIhEE12_Guard_allocD1Ev", (void*)&destroy_guard_alloc<unsigned char> },
    DEFINE_MODULE_SYMBOL(_ZNKSt8functionIFlvEEclEv),
    DEFINE_MODULE_SYMBOL(_ZNSt14_Function_baseD2Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt8functionIFlvEEC1ERKS1_),

    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE13find_first_ofEPKcj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE13find_first_ofEPKcjj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4copyEPcjj),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE7compareEjjPKc),
    DEFINE_MODULE_SYMBOL(_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE8_M_checkEjPKc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_constructEjc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE14_M_replace_auxEjjjc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE5eraseEjj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6appendEjc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6assignEPKcj),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6resizeEjc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_S_assignEPcjc),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC1EPKcjRKS3_),
    DEFINE_MODULE_SYMBOL(_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE20resize_and_overwriteIRZNS_9to_stringElEUlPcjE_EEvjT_),
    { "_ZNSt7__cxx119to_stringEl", (void*)&construct_to_string_result_long },

    // std::deque<char>/<double>/<basic_string<char>> and std::stack<T, deque<T>>
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIcSaIcEEC2Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIcSaIcEED2Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIcSaIcEE15_M_create_nodesEPPcS3_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIcSaIcEE16_M_destroy_nodesEPPcS3_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIcSaIcEE17_M_initialize_mapEj),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIdSaIdEEC2Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIdSaIdEED2Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIdSaIdEE15_M_create_nodesEPPdS3_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIdSaIdEE16_M_destroy_nodesEPPdS3_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseIdSaIdEE17_M_initialize_mapEj),
    { "_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EEC2Ev", (void*)&construct_deque_string },
    { "_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EEC2EOS7_", (void*)&move_construct_deque_string },
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EED2Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE15_M_create_nodesEPPS5_S9_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_destroy_nodesEPPS5_S9_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_M_initialize_mapEj),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_Deque_impl_dataC1EOS8_),
    DEFINE_MODULE_SYMBOL(_ZNSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_Deque_impl_dataC1ERKS8_),
    DEFINE_MODULE_SYMBOL(_ZSt4swapINSt11_Deque_baseINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS6_EE16_Deque_impl_dataEENSt9enable_ifIXsrSt6__and_IJSt6__not_ISt15__is_tuple_likeIT_EESt21is_move_constructibleISE_ESt18is_move_assignableISE_EEE5valueEvE4typeERSE_SO_),
    { "_ZNSt15_Deque_iteratorIcRcPcEmmEv", (void*)&decrement_deque_iterator_char },
    { "_ZNSt15_Deque_iteratorIdRdPdEmmEv", (void*)&decrement_deque_iterator_double },
    { "_ZNSt15_Deque_iteratorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERS5_PS5_EmmEv", (void*)&decrement_deque_iterator_string },
    { "_ZStmiRKSt15_Deque_iteratorIcRcPcES4_", (void*)&subtract_deque_iterators_char },
    { "_ZStmiRKSt15_Deque_iteratorIdRdPdES4_", (void*)&subtract_deque_iterators_double },
    { "_ZStmiRKSt15_Deque_iteratorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERS5_PS5_ESA_", (void*)&subtract_deque_iterators_string },
    { "_ZNSt5dequeIcSaIcEED1Ev", (void*)&destroy_deque_char },
    { "_ZNSt5dequeIcSaIcEE4backEv", (void*)&back_of_deque_char },
    { "_ZNSt5dequeIcSaIcEE8pop_backEv", (void*)&pop_back_deque_char },
    { "_ZNSt5dequeIcSaIcEE9push_backERKc", (void*)&push_back_deque_char },
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIcSaIcEE15_M_pop_back_auxEv),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIcSaIcEE16_M_push_back_auxIJRKcEEEvDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIcSaIcEE16_M_push_back_auxIJcEEEvDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIcSaIcEE17_M_reallocate_mapEjb),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIcSaIcEE22_M_reserve_map_at_backEj),
    { "_ZNSt5dequeIcSaIcEE12emplace_backIJcEEERcDpOT_", (void*)&emplace_back_deque_char },
    { "_ZNSt5dequeIdSaIdEED1Ev", (void*)&destroy_deque_double },
    { "_ZNSt5dequeIdSaIdEE4backEv", (void*)&back_of_deque_double },
    { "_ZNSt5dequeIdSaIdEE8pop_backEv", (void*)&pop_back_deque_double },
    { "_ZNSt5dequeIdSaIdEE9push_backERKd", (void*)&push_back_deque_double },
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIdSaIdEE15_M_pop_back_auxEv),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIdSaIdEE16_M_push_back_auxIJRKdEEEvDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIdSaIdEE16_M_push_back_auxIJdEEEvDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIdSaIdEE17_M_reallocate_mapEjb),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeIdSaIdEE22_M_reserve_map_at_backEj),
    { "_ZNSt5dequeIdSaIdEE12emplace_backIJdEEERdDpOT_", (void*)&emplace_back_deque_double },
    { "_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EED1Ev", (void*)&destroy_deque_string },
    { "_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE4backEv", (void*)&back_of_deque_string },
    { "_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE5clearEv", (void*)&clear_deque_string },
    { "_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE9pop_frontEv", (void*)&pop_front_deque_string },
    { "_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE9push_backERKS5_", (void*)&push_back_deque_string },
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_pop_front_auxEv),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_push_back_auxIJRKS5_EEEvDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE16_M_push_back_auxIJS5_EEEvDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE17_M_reallocate_mapEjb),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE22_M_reserve_map_at_backEj),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE15_M_destroy_dataESt15_Deque_iteratorIS5_RS5_PS5_ESB_RKS6_),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE15_M_erase_at_endESt15_Deque_iteratorIS5_RS5_PS5_E),
    DEFINE_MODULE_SYMBOL(_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE19_M_destroy_data_auxESt15_Deque_iteratorIS5_RS5_PS5_ESB_),
    { "_ZNSt5dequeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaIS5_EE12emplace_backIJS5_EEERS5_DpOT_", (void*)&emplace_back_deque_string },
    { "_ZNSt5stackIcSt5dequeIcSaIcEEEC1IS2_vEEv", (void*)&construct_stack_char },
    { "_ZNSt5stackIdSt5dequeIdSaIdEEEC1IS2_vEEv", (void*)&construct_stack_double },

    // std::vector<const char*> / vector<pair<string,bool>> internals
    DEFINE_MODULE_SYMBOL(_ZNKSt6vectorIPKcSaIS1_EE12_M_check_lenEjS1_),
    { "_ZNSt12_Vector_baseIPKcSaIS1_EED2Ev", (void*)&destroy_vector_base_of_cstrs },
    { "_ZNSt6vectorIPKcSaIS1_EE12_Guard_allocD1Ev", (void*)&(destroy_guard_alloc<const char*>) },
    { "_ZNSt6vectorIPKcSaIS1_EE12emplace_backIJS1_EEERS1_DpOT_", (void*)&emplace_back_cstr_vector },
    { "_ZNSt6vectorIPKcSaIS1_EE17_M_realloc_appendIJS1_EEEvDpOT_", (void*)&realloc_append_cstr_vector },
    { "_ZNSt6vectorIPKcSaIS1_EE4backEv", (void*)&back_of_cstr_vector },
    { "_ZNSt6vectorIPKcSaIS1_EE7reserveEj", (void*)&reserve_cstr_vector },
    { "_ZNSt15__new_allocatorIPKcE8allocateEjPKv", (void*)&allocate_cstr_storage },
    DEFINE_MODULE_SYMBOL(_ZNKSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE12_M_check_lenEjPKc),
    { "_ZNKSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE5emptyEv", (void*)&empty_string_bool_pair_vector },
    { "_ZNSt12_Vector_baseISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EED2Ev", (void*)&destroy_vector_base_of_string_bool_pairs },
    { "_ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE12_Guard_allocD1Ev", (void*)&(destroy_guard_alloc<StringBoolPair>) },
    { "_ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE12emplace_backIJS7_EEERS7_DpOT_", (void*)&emplace_back_string_bool_pair_vector },
    DEFINE_MODULE_SYMBOL(_ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE15_M_erase_at_endEPS7_),
    { "_ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE17_M_realloc_appendIJS7_EEEvDpOT_", (void*)&realloc_append_string_bool_pair_vector },
    DEFINE_MODULE_SYMBOL(_ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EE4backEv),
    { "_ZNSt6vectorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESaIS7_EED1Ev", (void*)&destroy_vector_of_string_bool_pairs },
    { "_ZNSt15__new_allocatorISt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbEE8allocateEjPKv", (void*)&allocate_string_bool_pair_storage },
    { "_ZNSt15__new_allocatorIPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEE8allocateEjPKv", (void*)&allocate_string_ptr_storage },
    { "_ZNSt15__new_allocatorIPcE8allocateEjPKv", (void*)&allocate_char_ptr_storage },
    { "_ZNSt15__new_allocatorIPdE8allocateEjPKv", (void*)&allocate_double_ptr_storage },

    // std::_Rb_tree<string, pair<const string,string>, ...> - std::map<string,string> internals
    DEFINE_MODULE_SYMBOL(_ZNKSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_lower_boundEPSt18_Rb_tree_node_baseSG_RS7_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE10_Auto_node9_M_insertES6_IPSt18_Rb_tree_node_baseSH_E),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE10_Auto_nodeD1Ev),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE11lower_boundERS7_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE12_M_drop_nodeEPSt13_Rb_tree_nodeIS8_E),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_create_nodeIJRKSt21piecewise_construct_tSt5tupleIJOS5_EESJ_IJEEEEEPSt13_Rb_tree_nodeIS8_EDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_create_nodeIJRKSt21piecewise_construct_tSt5tupleIJRS7_EESJ_IJEEEEEPSt13_Rb_tree_nodeIS8_EDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE14_M_insert_nodeEPSt18_Rb_tree_node_baseSG_PSt13_Rb_tree_nodeIS8_E),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE22_M_emplace_hint_uniqueIJRKSt21piecewise_construct_tSt5tupleIJOS5_EESJ_IJEEEEESt17_Rb_tree_iteratorIS8_ESt23_Rb_tree_const_iteratorIS8_EDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE22_M_emplace_hint_uniqueIJRKSt21piecewise_construct_tSt5tupleIJRS7_EESJ_IJEEEEESt17_Rb_tree_iteratorIS8_ESt23_Rb_tree_const_iteratorIS8_EDpOT_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE24_M_get_insert_unique_posERS7_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE29_M_get_insert_hint_unique_posESt23_Rb_tree_const_iteratorIS8_ERS7_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE4findERS7_),
    DEFINE_MODULE_SYMBOL(_ZNSt8_Rb_treeINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt4pairIKS5_S5_ESt10_Select1stIS8_ESt4lessIS5_ESaIS8_EE8_M_eraseEPSt13_Rb_tree_nodeIS8_E),

    // std::map<string,string>::operator[] + its map-node piecewise-construct path
    DEFINE_MODULE_SYMBOL(_ZNSt3mapINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES5_St4lessIS5_ESaISt4pairIKS5_S5_EEEixEOS5_),
    DEFINE_MODULE_SYMBOL(_ZNSt3mapINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES5_St4lessIS5_ESaISt4pairIKS5_S5_EEEixERS9_),
    DEFINE_MODULE_SYMBOL(_ZNSt4pairIKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES5_EC1IJRS6_EJEEESt21piecewise_construct_tSt5tupleIJDpT_EESB_IJDpT0_EE),
    { "_ZSt19piecewise_construct", (void*)&std::piecewise_construct },

    // std::shared_ptr<T> control block - generic; see the comment above assign_shared_count_atomic
    // for why the raw-pointer constructor itself is deliberately not registered here.
    { "_ZNSt14__shared_countILN9__gnu_cxx12_Lock_policyE1EEaSERKS2_", (void*)&assign_shared_count_atomic },
    { "_ZNSt14__shared_countILN9__gnu_cxx12_Lock_policyE2EEaSERKS2_", (void*)&assign_shared_count_mutex },
    DEFINE_MODULE_SYMBOL(_ZTVSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE),
    DEFINE_MODULE_SYMBOL(_ZTVSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE),
    DEFINE_MODULE_SYMBOL(_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE10_M_destroyEv),
    DEFINE_MODULE_SYMBOL(_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE10_M_releaseEv),
    DEFINE_MODULE_SYMBOL(_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE1EE19_M_release_last_useEv),
    DEFINE_MODULE_SYMBOL(_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE10_M_destroyEv),
    DEFINE_MODULE_SYMBOL(_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE10_M_releaseEv),
    DEFINE_MODULE_SYMBOL(_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE19_M_release_last_useEv),

    // std::function<int(char*, unsigned int)>
    { "_ZNKSt8functionIFiPcjEEclES0_j", (void*)&call_char_buffer_fn },
    { "_ZNSt8functionIFiPcjEE4swapERS2_", (void*)&swap_char_buffer_fn },
    { "_ZNSt8functionIFiPcjEEC1ERKS2_", (void*)&construct_char_buffer_fn_copy },
    { "_ZNSt8functionIFiPcjEEaSEDn", (void*)&assign_char_buffer_fn_nullptr },

    // Misc algorithm/iterator template instantiations
    { "_ZSt14__copy_move_a2ILb0EPPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES7_S7_ET2_T0_T1_S8_", (void*)&copy_move_a2_string_ptr },
    { "_ZSt14__copy_move_a2ILb0EPPcS1_S1_ET2_T0_T1_S2_", (void*)&copy_move_a2_char_ptr },
    { "_ZSt14__copy_move_a2ILb0EPPdS1_S1_ET2_T0_T1_S2_", (void*)&copy_move_a2_double_ptr },
    { "_ZSt14__relocate_a_1IPSt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbES8_SaIS7_EET0_T_SB_SA_RT1_", (void*)&relocate_string_bool_pairs },
    { "_ZSt23__copy_move_backward_a2ILb0EPPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES7_ET1_T0_S9_S8_", (void*)&copy_move_backward_a2_string_ptr },
    { "_ZSt23__copy_move_backward_a2ILb0EPPcS1_ET1_T0_S3_S2_", (void*)&copy_move_backward_a2_char_ptr },
    { "_ZSt23__copy_move_backward_a2ILb0EPPdS1_ET1_T0_S3_S2_", (void*)&copy_move_backward_a2_double_ptr },
    { "_ZSt23__copy_move_backward_a2ILb1EPSt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbES8_ET1_T0_SA_S9_", (void*)&move_backward_string_bool_pairs },
    { "_ZSt3maxIsET_St16initializer_listIS0_E", (void*)&max_short_init_list },
    { "_ZSt3minIsET_St16initializer_listIS0_E", (void*)&min_short_init_list },
    { "_ZSt9__advanceIPPNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEiEvRT_T0_St26random_access_iterator_tag", (void*)&advance_string_ptr_ptr },
    { "_ZSt9__advanceIPPciEvRT_T0_St26random_access_iterator_tag", (void*)&advance_char_ptr_ptr },
    { "_ZSt9__advanceIPPdiEvRT_T0_St26random_access_iterator_tag", (void*)&advance_double_ptr_ptr },
    { "_ZSt9iter_swapIN9__gnu_cxx17__normal_iteratorIPSt4pairINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEbESt6vectorIS9_SaIS9_EEEESE_EvT_T0_", (void*)&iter_swap_string_bool_pair_vector },
    { "_ZSt9transformIN9__gnu_cxx17__normal_iteratorIPcNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEEES9_PFiiEET0_T_SD_SC_T1_", (void*)&transform_char_to_upper },
    { "_ZSt4swapISt9_Any_dataENSt9enable_ifIXsrSt6__and_IJSt6__not_ISt15__is_tuple_likeIT_EESt21is_move_constructibleIS5_ESt18is_move_assignableIS5_EEE5valueEvE4typeERS5_SF_", (void*)&swap_any_data },
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
