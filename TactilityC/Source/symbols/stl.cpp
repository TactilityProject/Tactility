#include <private/elf_symbol.h>
#include <cstddef>

#include <symbols/stl.h>

#include <bits/functexcept.h>

#include <tactility/module.h>

extern "C" {

// std::map / std::set red-black tree non-template helpers.
// We use the mangled names directly (same pattern as string.cpp) to avoid
// ambiguity from the overloaded const/non-const variants in stl_tree.h.
void* _ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base(void*);
void* _ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base(void*);
void  _ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_(bool, void*, void*, void*);

}

const esp_elfsym stl_symbols[] = {
    // Note: You have to use the mangled names here
    { "_ZSt17__throw_bad_allocv", (void*)&(std::__throw_bad_alloc) },
    { "_ZSt28__throw_bad_array_new_lengthv", (void*)&(std::__throw_bad_array_new_length) },
    { "_ZSt25__throw_bad_function_callv", (void*)&(std::__throw_bad_function_call) },
    { "_ZSt20__throw_length_errorPKc", (void*)&(std::__throw_length_error) },
    { "_ZSt19__throw_logic_errorPKc", (void*)&std::__throw_logic_error },
    { "_ZSt24__throw_out_of_range_fmtPKcz", (void*)&std::__throw_out_of_range_fmt },
    // std::map / std::set (red-black tree internals)
    DEFINE_MODULE_SYMBOL(_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base),
    DEFINE_MODULE_SYMBOL(_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base),
    DEFINE_MODULE_SYMBOL(_ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_),
    // delimiter
    ESP_ELFSYM_END
};
