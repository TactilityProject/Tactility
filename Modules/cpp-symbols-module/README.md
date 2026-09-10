# cpp-symbols-module

Exports the C++ runtime/ABI symbols that side-loaded ELF apps need but that don't come from any
single library header - compiler-generated helpers (`operator new`/`delete`, vtable guard
variables) and libstdc++ internals that are normally only reachable through template
instantiation, not a plain function call. Apps still `#include <new>`/`<map>`/`<string>` etc.
directly; this module only makes sure the actual out-of-line definitions resolve when their ELF
is loaded into the firmware.

ESP32-only: several of these symbols are mangled for a 32-bit ABI (`j` = `unsigned int`, used
here to represent `size_t`). On a 64-bit host those would mangle differently (e.g. `_Znwm`
instead of `_Znwj`), so this module doesn't build for the POSIX simulator.

## Supported symbols

### Compiler/runtime ABI support

- `operator new(unsigned int)` / `operator delete(void*, unsigned int)` (`_Znwj` / `_ZdlPvj`)
- `std::nothrow`
- `__cxa_pure_virtual` - called through a pure-virtual slot before a derived class's vtable is
  fully constructed; see [Bare metal C++](https://arobenko.github.io/bare_metal_cpp/).
- `__cxa_guard_acquire` / `__cxa_guard_release` / `__cxa_guard_abort` / `__cxa_guard_dummy` -
  thread-safe one-time initialization of function-local `static` variables.

### libstdc++ exception helpers

Out-of-line `std::__throw_*` functions libstdc++ headers call instead of throwing directly, to
keep the throw site small:

- `std::__throw_bad_alloc`
- `std::__throw_bad_array_new_length`
- `std::__throw_bad_function_call`
- `std::__throw_length_error`
- `std::__throw_logic_error`
- `std::__throw_out_of_range_fmt`
- `std::__throw_system_error`

### `std::map` / `std::set` (red-black tree internals)

Non-template helpers shared by every `std::map`/`std::set` instantiation:

- `std::_Rb_tree_increment` / `std::_Rb_tree_decrement`
- `std::_Rb_tree_insert_and_rebalance`

### `std::string`

Most of the common out-of-line member set (this class ends up entirely out-of-line for any app
built at C++17 or earlier - see `extern template class basic_string<char>` in libstdc++'s
`bits/basic_string.tcc`):

- `_M_replace_cold`, `_M_replace`, `_M_mutate`, `_M_append`, `_M_erase`, `_M_assign`,
  `_M_create`, `_M_dispose`, `_M_construct<const char*>`
- `substr`, `find`, `reserve`, `append` (both overloads), `assign`, `push_back`, `pop_back`,
  `operator=(basic_string&&)`
- `_S_copy`, `_S_move` (static helpers)
- Free functions returning `basic_string<char>` by value: `__str_concat`, `operator+(basic_string
  const&, const char*)`

- `(basic_string const&, pos, len)` and `(const char*, allocator const&)` constructors -
  implemented directly rather than exported by address (see below), since GCC doesn't emit a
  standalone definition for either.

**Not supported: the move constructor.** Like the two constructors above, GCC never emits a
standalone out-of-line definition for it in a C++20+ build - it's a pure header-inline forwarder
with no real function body to take the address of, even when this module's own code is made to
call it directly. If it turns out to be needed, implement it the same way as the other two: write
a small function whose body does the equivalent construction via placement-new (`new (self)
std::string(...)`), and register its address manually under the mangled name(s) in `SYMBOLS[]`
(the compiler inlines the real header logic into that function, producing a genuine addressable
symbol - the same trick used for `_M_replace_cold` and friends, except *we* supply the body
instead of pointing at one the library already provides). An app whose loaded ELF needs it will
still fail with `Can't find common ...C1.../...C2.../...C5...`.

## Adding a new symbol

1. Find the mangled name (`nm`/`c++filt`, or the linker's "undefined reference" error from an
   app build).
2. Add an `extern "C"` declaration for it in `source/module.cpp` if the name isn't already a
   valid identifier you can reference directly (mangled names usually are, e.g. `_ZSt19...`).
3. Add a `DEFINE_MODULE_SYMBOL(...)` entry (or a manual `{ "mangled_name", (void*)&expr }` pair
   when the address isn't reachable through the mangled identifier itself, e.g. `std::nothrow`
   or the `__throw_*` functions).

## License

This module is licensed under the [Apache v2.0](LICENSE-Apache-2.0.md) license.
