// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

/**
 * Applies @a overlay (a NULL-terminated array of "NAME=VALUE" strings, e.g.
 * AppStartContext::environment) onto @a env in place: each entry either replaces the existing
 * same-name entry in @a env or is appended. Used by app_manager_start_internal() to combine a new
 * app instance's inherited-from-parent environment (the base) with its own explicit environment
 * (the overlay).
 */
void app_env_apply(std::vector<std::string>& env, const char* const* overlay);
