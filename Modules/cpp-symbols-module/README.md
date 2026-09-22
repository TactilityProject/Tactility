# cpp-symbols-module

Exports the C++ runtime/ABI symbols that side-loaded ELF apps need but that don't come from any
single library header - compiler-generated helpers (`operator new`/`delete`, vtable guard
variables) and libstdc++ internals that are normally only reachable through template
instantiation, not a plain function call. Apps still `#include <new>`/`<map>`/`<string>` etc.
directly; this module only makes sure the actual out-of-line definitions resolve when their ELF
is loaded into the firmware.

Most of this module is ESP32-only: several of these symbols are mangled for a 32-bit ABI (`j` =
`unsigned int`, used here to represent `size_t`) that only applies there - a 64-bit host mangles
them differently (e.g. `_Znwm` instead of `_Znwj`). Those sections are gated behind `#ifdef
ESP_PLATFORM` in `source/module.cpp`; symbols outside that gate (e.g. `__cxa_pure_virtual`,
`__cxa_guard_*`) build and export on the POSIX simulator too.

## Supported symbols

### Compiler/runtime ABI support

- `operator new(unsigned int)` / `operator delete(void*, unsigned int)` (`_Znwj` / `_ZdlPvj`)
- `operator new[](unsigned int)` / `operator delete[](void*, unsigned int)` (`_Znaj` / `_ZdaPvj`)
- `operator delete(void*)` / `operator delete[](void*)` (`_ZdlPv` / `_ZdaPv`) - the unsized forms,
  used instead of the above when the compiler determines no size is needed
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

### `std::deque<char>` / `<double>` / `<basic_string<char>>`, `std::stack`

Non-template `_Deque_base`/`_Deque_iterator` internals plus each element type's own out-of-line
member set (push_back/pop_back/emplace_back, map reallocation, iterator arithmetic). Needed by any
app using `std::stack` (whose default underlying container is `std::deque`). None of this has an
`extern template` instantiation in libstdc++.a, so every member here is forced into existence by
a wrapper that calls the real public deque/stack API on a real object (see `source/module.cpp`).

### `std::_Rb_tree<std::string, ...>`, `std::map<std::string, std::string>`

The `std::map<string, string>` instantiation's own non-template-shared internals (on top of the
generic `_Rb_tree_increment`/`_Rb_tree_insert_and_rebalance` helpers above), plus
`operator[]`/piecewise-construct support for in-place node construction.

### `std::vector<const char*>`, `std::vector<std::pair<std::string, bool>>`

Same `_M_check_len`/`_Guard_alloc`/`emplace_back`/`_M_realloc_append` pattern as `vector<string>`
and `vector<unsigned char>` above, for two more element types apps instantiate over.

### `std::shared_ptr<T>` control block

`__shared_count::operator=`, `_Sp_counted_base`'s virtual dispatch, and its vtable, for both lock
policies (atomic and mutex-based - which one an app's build selects isn't controlled by this
firmware, so both are exported). Forced via `std::shared_ptr<int>` rather than any real app type,
since none of these three carry a pointee type in their mangled name.

**Not exported: the raw-pointer constructor** (`__shared_count<Lp>::__shared_count<T*>(T*)`),
because its mangled name does embed the pointee type `T` - no single firmware-side entry can serve
every app's `shared_ptr<AppDefinedType>`. This isn't a gap in practice: the app's own compiler
already emits a local, weak-linkage definition of that exact constructor in the app's own ELF
(standard behavior for an implicitly-instantiated template), and the ELF loader falls back to an
app-local definition whenever firmware doesn't provide one, so it resolves without any firmware
export at all. Confirmed by loading an app holding a `shared_ptr` to one of its own types with
nothing shared_ptr-specific exported here beyond the generic pieces above.

### `std::function<int(char*, unsigned int)>`

`operator()`, `swap`, copy constructor, and `operator=(nullptr)` for this one call signature.

### Misc algorithm/iterator template instantiations

`__copy_move_a2`, `__copy_move_backward_a2`, `__relocate_a_1`, `__advance`, `iter_swap`,
`transform`, `std::min`/`std::max<short>` - pulled in by `std::stack`/`vector<pair<...>>` usage
above rather than needed standalone.

## Adding a new symbol

1. Find the mangled name. `TactilityApps/Tools/check-app-symbols.py <path/to/App.app.elf>` reports
   every symbol an app's ELF actually needs firmware to resolve - it already excludes symbols the
   app's own ELF defines locally (e.g. an implicitly-instantiated template like a `shared_ptr<T>`
   constructor over an app-defined `T`), so if it doesn't list something, it doesn't need adding
   here even if the mangled name shows up in the ELF's relocations.
2. Add an `extern "C"` declaration for it in `source/module.cpp` if the name isn't already a
   valid identifier you can reference directly (mangled names usually are, e.g. `_ZSt19...`). If a
   plain `extern` declaration compiles but fails to *link*, the symbol likely has no `extern
   template` instantiation anywhere in libstdc++.a (true for `deque`, `vector<pair<...>>`,
   `shared_ptr`'s control block, `std::function`, and most algorithm templates) - write a small
   wrapper that calls the real public API on a real object instead (see any of the existing
   `construct_*`/`destroy_*` functions in this file for the pattern), and register the wrapper's
   address rather than the mangled name directly.
3. Add a `DEFINE_MODULE_SYMBOL(...)` entry (or a manual `{ "mangled_name", (void*)&expr }` pair
   when the address isn't reachable through the mangled identifier itself, e.g. `std::nothrow`
   or the `__throw_*` functions).
4. Rebuild the firmware and confirm with `nm`/`check-app-symbols.py` that the symbol resolves to
   a real, non-null defined address - a reference that merely compiles and links without error can
   still silently resolve to the wrong (e.g. an unrelated weak-undefined stub) or a null definition
   if something else in the link graph shifts which archive member satisfies it first.

## License

This module is licensed under the [Apache v2.0](LICENSE-Apache-2.0.md) license.
