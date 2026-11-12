# Coding Style

Two conventions coexist; which one to use depends on the project layer:

- **C code**: `lower_snake_case` for files, functions, variables. `UpperCamelCase` for types. Files in `source/`, `include/`, `private/` directories.
- **C++ code**: `UpperCamelCase` for files and types. `lowerCamelCase` for functions. `lower_snake_case` for namespaces. Files in `Source/`, `Include/`, `Private/` directories.
  Folder names reflect the casing of the C++ symbols they hold: a folder for a namespace is lower case and a file is named after the type it defines (e.g. `TactilityCpp/Memory.h` for `tt::newObject()`).
  Exception: wrappers of standard library symbols keep the standard library's lower case names, for compatibility (e.g. `tactility/std/vector.hpp` for `tt::std::vector`).
  Inside `namespace tt`, `std::` resolves to `tt::std` once a `TactilityCpp/std/` header is included. Prefer the `tt::std` wrappers there, and use `::std::` for standard library symbols that have no wrapper.

For projects that emit C headers and have a C++ implementation file: the internal C++ function naming should be snake_case.

Formatting is enforced by `.clang-format` (LLVM-based, 4-space indent, no column limit).
Never throw exceptions — use return types for error handling. Use `enum class` over plain `enum` when writing C++ code.
Do not add redundant null checks for parameters with an explicit non-null precondition.

Code Comments (important!):

- Should be minimal, but must contain critical information.
- Must not contain interjections " - " or ";"
- Must not explain how code was before, or how it was changed.
- Must avoid explaining what was not implemented.
- Must not refer to designs of other subsystems.
