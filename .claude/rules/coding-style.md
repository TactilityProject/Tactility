# Coding Style

Two conventions coexist; which one to use depends on the project layer:

- **C code**: `lower_snake_case` for files, functions, variables. `UpperCamelCase` for types. Files in `source/`, `include/`, `private/` directories.
- **C++ code**: `UpperCamelCase` for files and types. `lowerCamelCase` for functions. Files in `Source/`, `Include/`, `Private/` directories.

For projects that emit C headers and have a C++ implementation file: the internal C++ function naming should be snake_case.

Formatting is enforced by `.clang-format` (LLVM-based, 4-space indent, no column limit).
Never throw exceptions — use return types for error handling. Use `enum class` over plain `enum` when writing C++ code.
Do not add redundant null checks for parameters with an explicit non-null precondition.

Code Comments (important!):

- Should be minimal, but must contain critical information.
- Must not explain how code was before, or how it was changed.
- Should avoid explaining what was not implemented.
- Must avoid referring to designs of other subsystems.
- Must avoid interjections: avoid hyphens or braces to interject.
