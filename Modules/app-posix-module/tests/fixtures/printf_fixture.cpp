// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <cstdio>

// Deliberately not linked against app-module: printf()'s dynamic symbol resolution must reach
// Tactility's own override in the loading process, exactly like a real loaded app's would.
extern "C" int32_t main(int, char*[]) {
    printf("hello from dlopen'd app\n");
    // %s, not a literal: a literal fprintf compiles to fwrite(), which bypasses the wrap.
    fprintf(stderr, "%s\n", "oops from dlopen'd app");
    return 0;
}
