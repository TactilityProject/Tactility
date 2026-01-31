#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "error.h"
#include <stdbool.h>

struct ModuleParent;
struct ModuleParentPrivate;

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

    struct {
        bool started;
        struct ModuleParent* parent;
    } internal;
};

/**
 * A module parent is a collection of modules that can be loaded and unloaded at runtime.
 */
struct ModuleParent {
    /** The name of the parent module, for logging/debugging purposes */
    const char* name;
    struct ModuleParentPrivate* module_parent_private;
};

/**
 * @brief Initialize the module parent.
 * @warn This function does no validation on input or state.
 * @param parent parent module
 * @return ERROR_NONE if successful, ERROR_OUT_OF_MEMORY if allocation fails
 */
error_t module_parent_construct(struct ModuleParent* parent);

/**
 * @brief Deinitialize the module parent. Must have no children when calling this.
 * @warn This function does no validation on input.
 * @param parent parent module
 * @return ERROR_NONE if successful or ERROR_INVALID_STATE if the parent has children
 */
error_t module_parent_destruct(struct ModuleParent* parent);

/**
 * @brief Set the parent of the module.
 * @warning must call before module_start()
 * @param module module
 * @param parent nullable parent module
 * @return ERROR_NONE if successful, ERROR_INVALID_STATE if the module is already started
 */
error_t module_set_parent(struct Module* module, struct ModuleParent* parent);

/**
 * @brief Start the module.
 * @param module module
 * @return ERROR_NONE if already started, ERROR_INVALID_STATE if the module doesn't have a parent, or otherwise it returns the result of the module's start function
 */
error_t module_start(struct Module* module);

/**
 * @brief Check if the module is started.
 * @param module module to check
 * @return true if the module is started, false otherwise
 */
bool module_is_started(struct Module* module);

/**
 * @brief Stop the module.
 * @param module module
 * @return ERROR_NONE if successful or if the module wasn't started, or otherwise it returns the result of the module's stop function
 */
error_t module_stop(struct Module* module);

#ifdef __cplusplus
}
#endif
