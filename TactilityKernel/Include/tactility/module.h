// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#define MODULE_SYMBOL_TERMINATOR { nullptr, nullptr }
#else
#define MODULE_SYMBOL_TERMINATOR { NULL, NULL }
#endif

#define DEFINE_MODULE_SYMBOL(symbol) { #symbol, (void*)&symbol }

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
     * A function to initialize the module.
     * Should never be NULL.
     * This can be used to load drivers, initialize hardware, etc.
     * @return ERROR_NONE if successful
     */
    error_t (*start)(void);

    /**
     * Deinitializes the module.
     * Should never be NULL.
     * @return ERROR_NONE if successful
     */
    error_t (*stop)(void);

    /**
     * A list of symbols exported by the module.
     * Should be terminated by MODULE_SYMBOL_TERMINATOR.
     * Can be a NULL value.
     */
    const struct ModuleSymbol* symbols;

    struct {
        bool started;
    } internal;
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
 * @return ERROR_NONE if successful
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
 * @return ERROR_NONE if successful or if the module wasn't started, or otherwise it returns the result of the module's stop function
 */
error_t module_stop(struct Module* module);

/**
 * @brief Construct, add and start a module.
 * @param module module
 * @return ERROR_NONE if successful
 */
error_t module_construct_add_start(struct Module* module);

/**
 * @brief Check if the module is started.
 * @param module module to check
 * @return true if the module is started, false otherwise
 */
bool module_is_started(struct Module* module);

/**
 * @brief Resolve a symbol from the module.
 * @details The module must be started for symbol resolution to succeed.
 * @param module module
 * @param symbol_name name of the symbol to resolve
 * @param symbol_address pointer to store the address of the resolved symbol
 * @return true if the symbol was found and the module is started, false otherwise
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

#ifdef __cplusplus
}
#endif
