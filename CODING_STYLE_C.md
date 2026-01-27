# C coding Style

## Files & Folders

### Files

Files are lower snake case.

- Files: `^[0-9a-z_]+$`
- Directories: `^[0-9a-z_]+$`

Example:
```c
some_feature.c
some_feature.h
```

### Folders

Project folders include:
- `source` for source files and public header files
- `private` for private header files
- `include` for projects that require separate header files

## C language

### Macros and consts

These are all upper snake case:

```c
#define MY_CONST 1
#define PRINT_SOMETHING() printf("something")
const int ANOTHER_CONST = 1;
```

### Variables

Variable names and function parameters are lower snake case:

```c
int some_variable;
```

### Enumerations and Structs

Enums and struct types are upper camel case.
Its fields use lower snake case.

```c
struct ThreadData {
    int some_attribute;
};
```

```c
enum SomeResult  {
    Ok,
    NotSupported,
    Error
};
```

### Function names

Function names are lower snake case.

Example:

```c
void get_limit() {
    // ...
}
```

If a set of functions relates to a `struct`, the CamelCase struct name is converted to a snake_case_ prefix for the function name:

```c
struct TextMessage {
    // ...
};

void text_message_set(struct TextMessage* message) { /* ... */ }

void text_message_get(struct TextMessage* message) { /* ... */ }

```

### Typedef of simple value types

Typedefs for simple value types (such as int, int64_t, char) are lower snake case.
They are postfixed with `_t`

Examples:

```c
typedef uint32_t thread_id_t;
```

### Function comments

```c
/**
 * @brief Validates a number
 * @param[in] number the integer to validate
 * @return true if validation was succesful and there were no issues
 */
bool validate(int number);

/**
 * @brief Run the action.
 * @param timeout[in] the maximum time the task should run
 * @retval ERROR_TIMEOUT when the task couldn't be completed on time
 * @retval ERROR_NONE when the task completed successfully
 */
error_t runAction(TickType_t timeout);

/**
 * @brief Increase a number.
 * @param[inout] number
 */
void increase(int* number);

/**
 * A function with a longer description here.
 *
 * @brief short description
 */
void something();
```
