// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <cstdlib>
#include <string>

// Deliberately not linked against app-module: exit() must resolve against Tactility's own override
// in the loading process. The live std::string makes sure ending the task with it on the stack is safe.
extern "C" int32_t main(int, char*[]) {
    std::string live = "still on the stack";
    exit(static_cast<int>(live.size()));
}
