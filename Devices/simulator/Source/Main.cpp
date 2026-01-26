#include "Main.h"
#include <Tactility/Thread.h>
#include <Tactility/TactilityCore.h>

#include "FreeRTOS.h"
#include "task.h"

static const auto LOGGER = tt::Logger("FreeRTOS");

namespace simulator {

MainFunction mainFunction = nullptr;

void setMain(MainFunction newMainFunction) {
    mainFunction = newMainFunction;
}

static void freertosMainTask(void* parameter) {
    LOGGER.info("starting app_main()");
    assert(simulator::mainFunction);
    mainFunction();
    LOGGER.info("returned from app_main()");
    vTaskDelete(nullptr);
}

void freertosMain() {
    BaseType_t task_result = xTaskCreate(
        freertosMainTask,
        "main",
        8192,
        nullptr,
        static_cast<UBaseType_t>(tt::Thread::Priority::Normal),
        nullptr
    );

    assert(task_result == pdTRUE);

    // Blocks forever
    vTaskStartScheduler();
}

} // namespace

/**
 * Assert implementation as defined in the FreeRTOSConfig.h
 * It allows you to set breakpoints and debug asserts.
 */
void vAssertCalled(unsigned long line, const char* const file) {
    volatile uint32_t set_to_nonzero_in_debugger_to_continue = 0;
    LOGGER.error("Assert triggered at {}:{}", file, line);
    taskENTER_CRITICAL();
    {
        // Step out by attaching a debugger and setting set_to_nonzero_in_debugger_to_continue
        while (set_to_nonzero_in_debugger_to_continue == 0) {
            // NO-OP
        }
    }
    taskEXIT_CRITICAL();
}

