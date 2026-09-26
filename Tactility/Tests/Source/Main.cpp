#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "FreeRTOS.h"
#include "task.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

// app-module overrides chdir() with an app-aware version, which needs the kernel to be running
extern "C" int __real_chdir(const char* path);

#include <tactility/check.h>
#include <tactility/dts.h>
#include <tactility/kernel_init.h>

typedef struct {
    int argc;
    char** argv;
    int result;
} TestTaskData;

// From the relevant platform
extern "C" struct Module platform_posix_module;

void test_task(void* parameter) {
    auto* data = (TestTaskData*)parameter;

    doctest::Context context;

    context.applyCommandLine(data->argc, data->argv);

    // overrides
    context.setOption("no-breaks", true); // don't break in the debugger when assertions fail

    Module* dts_modules[] = { &platform_posix_module, nullptr };
    DtsDevice dts_devices[] = { DTS_DEVICE_TERMINATOR };
    check(kernel_init(dts_modules, dts_devices) == ERROR_NONE);

    data->result = context.run();

    vTaskEndScheduler();

    vTaskDelete(nullptr);
}

int main(int argc, char** argv) {
    // The POSIX platform registers its "data" filesystem relative to the working directory, and
    // tests that need temp files (e.g. the shell interpreter) require it. Running in a scratch
    // directory makes that independent of where the binary is started from.
    char scratch[] = "/tmp/tactility-tests-XXXXXX";
    if (mkdtemp(scratch) == nullptr || __real_chdir(scratch) != 0 || mkdir("data", 0755) != 0) {
        perror("Failed to set up the test working directory");
        return 1;
    }

    TestTaskData data = {
        .argc = argc,
        .argv = argv,
        .result = 0
    };

    BaseType_t task_result = xTaskCreate(
        test_task,
        "test_task",
        8192,
        &data,
        1,
        nullptr
    );

    if (task_result != pdPASS) {
        return 1;
    }

    vTaskStartScheduler();

    std::error_code ignored;
    std::filesystem::remove_all(scratch, ignored);

    return data.result;
}
