// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define MODULE_SYMBOL_TERMINATOR { nullptr, nullptr }
#else
#define MODULE_SYMBOL_TERMINATOR { NULL, NULL }
#endif

#define DEFINE_MODULE_SYMBOL(symbol) { #symbol, (void*)&symbol }
/** For an overloaded symbol (e.g. libc++/libstdc++'s float/double/long double math overloads),
 * where a bare `&symbol` is ambiguous - `type` picks the overload to take the address of. */
#define DEFINE_MODULE_SYMBOL_SIGNATURE(symbol, type) { #symbol, (void*)(type)&symbol }
/** Export `symbol` under a different exported `name`, for renamed symbols the loader must still resolve by their old name. */
#define DEFINE_MODULE_SYMBOL_ALIAS(name, symbol) { name, (void*)&symbol }

#ifdef __cplusplus
extern "C" {
#endif

/** A binary symbol like a function or a variable. */
struct ModuleSymbol {
    /** The name of the symbol. */
    const char* name;
    /** The address of the symbol. */
    const void* symbol;
};

struct Driver;
struct ModuleInternal;

/**
 * A module is a collection of drivers or other functionality that can be loaded and unloaded at runtime.
 */
struct Module {
    /**
     * The name of the module, for logging/debugging purposes
     * Should never be NULL.
     * Characters allowed: a-z A-Z 0-9 - _ .
     * Desirable format "platform-esp32", "lilygo-tdeck", etc.
     */
    const char* name;
    /**
     * A function to initialize the module, or NULL.
     * This can be used to load drivers, initialize hardware, etc.
     * @return ERROR_NONE if successful
     */
    error_t (*start)(void);
    /**
     * A function to deinitialize the module, or NULL.
     * @return ERROR_NONE if successful
     */
    error_t (*stop)(void);
    /**
     * An array of driver pointers, or NULL.
     * The list must be NULL-terminated.
     * When the module is started, these drivers are constructed and added to the module.
     * When the module is stopped, these drivers are removed from the module and destroyed.
     */
    struct Driver* const* drivers;
    /**
     * A list of symbols exported by the module.
     * Should be terminated by MODULE_SYMBOL_TERMINATOR.
     * Can be a NULL value.
     */
    const struct ModuleSymbol* symbols;
    /**
     * Internal state managed by the kernel.
     * Module implementers should initialize this to NULL.
     * Do not access or modify directly; use module_* functions.
     */
    struct ModuleInternal* internal;
};

/**
 * @brief Construct a module instance.
 * @param module module instance to construct
 * @return ERROR_NONE if successful
 */
error_t module_construct(struct Module* module);

/**
 * @brief Destruct a module instance.
 * @param module module instance to destruct
 * @return ERROR_NONE if successful
 */
error_t module_destruct(struct Module* module);

/**
 * @brief Add a module to the system.
 * @warning Only call this once. This function does not check if it was added before.
 * @param module module to add
 * @return ERROR_NONE if successful
 */
error_t module_add(struct Module* module);

/**
 * @brief Remove a module from the system.
 * @param module module to remove
 * @retval ERROR_NONE if successful
 * @retval ERROR_RESOURCE_BUSY the module is referenced (see module_get() and module_get_code()). It stays added.
 */
error_t module_remove(struct Module* module);

/**
 * @brief Start the module.
 * @param module module
 * @return ERROR_NONE if already started, or otherwise it returns the result of the module's start function
 */
error_t module_start(struct Module* module);

/**
 * @brief Stop the module.
 * @param module module
 * @retval ERROR_NONE if successful or if the module wasn't started
 * @retval ERROR_RESOURCE_BUSY the module is referenced (see module_get(), or module_get_code() for a
 * module that came from a package) or a driver of it is bound to a device. The module keeps running.
 * @return otherwise the result of the module's stop function
 */
error_t module_stop(struct Module* module);

/**
 * @brief Construct, add and start a module.
 * @param module module
 * @return ERROR_NONE if successful
 */
error_t module_construct_add_start(struct Module* module);

/**
 * @brief Tries to ensure the module is in a started state.
 * Calls module_construct if needed, calls module_add if needed, calls module_start if needed.
 * @param module the module
 * @return ERROR_NONE if module is in a started state
 */
error_t module_ensure_started(struct Module* module);

/**
 * @brief Tries to ensure the module is in a started state.
 * Calls module_stop if needed, calls module_remove, calls module_destruct if needed.
 * @param module the module
 * @return ERROR_NONE if module is in a destructed state
 */
error_t module_ensure_destructed(struct Module* module);

/**
 * @brief Check if the module is started.
 * Can be used when module isn't constructed yet.
 * @param module module to check
 * @return true if the module is started, false otherwise
 */
bool module_is_started(struct Module* module);

/**
 * @brief Check if the module was loaded from outside the firmware image (e.g. from an installed package).
 * Can be used when module isn't constructed yet.
 * @param module module to check
 * @return true if the module was marked external, false otherwise
 */
bool module_is_external(struct Module* module);

/**
 * @brief Mark the module as externally loaded or not.
 * @details The module must be constructed. The flag lives until the module is destructed.
 * @param module module to mark
 * @param external the new value of the flag
 * @return ERROR_NONE if successful, ERROR_INVALID_STATE if the module isn't constructed
 */
error_t module_set_external(struct Module* module, bool external);

/**
 * @brief Take a reference on a started module, so that it can't be stopped until module_put().
 * @details Use this while code or data of the module is used by something else, e.g. an ELF whose
 * relocations point into the module's symbols. Every successful call must be paired with one module_put().
 * @param module module to reference
 * @retval ERROR_NONE the module is referenced
 * @retval ERROR_INVALID_STATE the module isn't started, or is being stopped
 */
error_t module_get(struct Module* module);

/**
 * @brief Release a reference taken with a successful module_get() or module_resolve_symbol_global_get().
 * @param module module that was referenced
 */
void module_put(struct Module* module);

/**
 * @brief Take a reference on a module for code that was relocated against its symbols.
 * @details Unlike module_get(), this reference doesn't prevent module_stop(): relocated code only
 * needs the module's code and data to stay mapped, which only module_remove() would break. It does
 * prevent module_remove() for as long as the reference is held.
 * This is what an app's ELF gets for every module it resolves symbols from, so that the app can
 * still stop a module it was linked against (e.g. to take exclusive control of a device) and start
 * it again afterwards. Every successful call must be paired with one module_put_code().
 * @param module module to reference
 * @retval ERROR_NONE the module is referenced
 * @retval ERROR_INVALID_STATE the module isn't added, so its symbols can't be resolved
 */
error_t module_get_code(struct Module* module);

/**
 * @brief Release a reference taken with a successful module_get_code() or module_resolve_symbol_global_get_code().
 * @param module module that was referenced
 */
void module_put_code(struct Module* module);

/**
 * @brief Resolve a symbol from the module.
 * @details The module must be added with module_add() for symbol resolution to succeed. It doesn't have to be started.
 * @param module module
 * @param symbol_name name of the symbol to resolve
 * @param symbol_address pointer to store the address of the resolved symbol
 * @return true if the symbol was found and the module is added, false otherwise
 */
bool module_resolve_symbol(struct Module* module, const char* symbol_name, uintptr_t* symbol_address);

/**
 * @brief Resolve a symbol from any module
 * @details This function iterates through all started modules in the parent and attempts to resolve the symbol.
 * @param symbol_name name of the symbol to resolve
 * @param symbol_address pointer to store the address of the resolved symbol
 * @return true if the symbol was found, false otherwise
 */
bool module_resolve_symbol_global(const char* symbol_name, uintptr_t* symbol_address);

/**
 * @brief Same as module_resolve_symbol_global(), but also takes a reference on the module that has the symbol.
 * @details The resolve and the reference are one atomic step, so the module can't stop in between.
 * @param symbol_name name of the symbol to resolve
 * @param symbol_address pointer to store the address of the resolved symbol
 * @param out_module pointer to store the module that has the symbol. Call module_put() on it when done.
 * @return true if the symbol was found. The module is referenced then.
 */
bool module_resolve_symbol_global_get(const char* symbol_name, uintptr_t* symbol_address, struct Module** out_module);

/**
 * @brief Same as module_resolve_symbol_global(), but also takes a code reference on the module
 * that has the symbol (see module_get_code()).
 * @details Use this when the resolved symbol ends up in code that outlives the lookup (e.g. an ELF
 * relocation), where the module's code must stay mapped but where the module itself may still be
 * stopped and started again.
 * @param symbol_name name of the symbol to resolve
 * @param symbol_address pointer to store the address of the resolved symbol
 * @param out_module pointer to store the module that has the symbol. Call module_put_code() on it.
 * @return true if the symbol was found. The module is referenced then.
 */
bool module_resolve_symbol_global_get_code(const char* symbol_name, uintptr_t* symbol_address, struct Module** out_module);

#ifdef __cplusplus
}
#endif
