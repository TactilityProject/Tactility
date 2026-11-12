// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/module.h>

#include <vector>

/** The kernel modules whose symbols an ELF resolved while it was relocated. Each one is held with a code reference (module_get_code()). */
struct ModuleRefs {
    std::vector<Module*> modules;
};

/**
 * Collects the modules that the symbol resolver hands out into @a refs, until module_refs_end().
 * Blocks while another ELF is being relocated, because the resolver has no way to tell them apart.
 */
void module_refs_begin(ModuleRefs* refs);

void module_refs_end();

/** Releases the references that were collected in @a refs. */
void module_refs_release(ModuleRefs* refs);

/** Must be called before the first module_refs_begin() and after the last module_refs_end() respectively. */
void module_refs_construct();

void module_refs_destruct();
