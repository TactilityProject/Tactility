# C++ coding Style

The basic formatting rules are set in `.clang-format`. Use auto-formatting in your editor.

All code should target C++ language revision 20.

If you use CLion, please enable the setting called "Enable ClangFormat" under Settings > Editor > Code Style.

## Naming

### Files

Files are upper camel case.

- Files: `^[0-9a-zA-Z]+$`
- Directories: `^[0-9a-zA-Z]+$`
 
Example:
```c++
SomeFeature.cpp
SomeFeature.h
```

Private/internal headers are postfixed with `Private` before the file extension.
Like `SomeFeaturePrivate.h`

### Folders

Project folders include:
- `Source` for source files and public header files
- `Private` for private header files
- `Include` for projects that require separate header files

Sources for a specific namespace must go into a folder with the lowercase name of that namespace.
The only exception to this is the root namespace, as this doesn't require its own subfolder.

### Function names

Names are lower camel case.

Example:

```c++
void getLimit() {
    // ...
}
```

### Type names

Consts are lower camel case with capital letters.

Typedefs for structs, classes and datatype aliases are upper camel case.

Examples:

```c++
typedef uint32_t ThreadId;

const ThreadId id;

struct ThreadData {
    // ...
};
```

### Enumerations

`enum class` or `enum struct` is preferred over plain `enum` because of scoping. It's styled like this:

```c++
enum class  {
    Ok,
    NotSupported,
    Error
};
```

### Exceptions

Don't ever throw them. Make a return type that wraps all the error and success scenarios that are relevant.
