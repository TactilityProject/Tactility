#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ARCH_XTENSA)

#include "Tactility/kernel/PanicHandler.h"

#include <esp_debug_helpers.h>
#include <esp_attr.h>
#include <esp_memory_utils.h>
#include <esp_cpu.h>
#include <esp_cpu_utils.h>
#include <xtensa/xtruntime.h>

extern "C" {

/**
 * This static variable survives a crash reboot.
 * It is reset by the Boot app.
 */
static RTC_NOINIT_ATTR CrashData crashData;

void __real_esp_panic_handler(void* info);

void __wrap_esp_panic_handler(void* info) {

    esp_backtrace_frame_t frame = {
        .pc = 0,
        .sp = 0,
        .next_pc = 0,
        .exc_frame = nullptr
    };

    crashData.callstackLength = 0;

    esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
    crashData.callstack[0].pc = frame.pc;
#if CRASH_DATA_INCLUDES_SP
    crashData.callstack[0].sp = frame.sp;
#endif
    crashData.callstackLength++;

    uint32_t processed_pc = esp_cpu_process_stack_pc(frame.pc);
    bool pc_is_valid = esp_ptr_executable((void *)processed_pc);

    /* Ignore the first corrupted PC in case of InstrFetchProhibited on Xtensa */
    if (frame.exc_frame && ((XtExcFrame *)frame.exc_frame)->exccause == EXCCAUSE_INSTR_PROHIBITED) {
        pc_is_valid = true;
    }

    crashData.callstackCorrupted = !(esp_stack_ptr_is_sane(frame.sp) && pc_is_valid);

    while (
        frame.next_pc != 0 &&
        !crashData.callstackCorrupted
        && crashData.callstackLength < CRASH_DATA_CALLSTACK_LIMIT
    ) {
        if (esp_backtrace_get_next_frame(&frame)) {
            // Validate the current frame
            uint32_t processed_frame_pc = esp_cpu_process_stack_pc(frame.pc);
            bool frame_pc_is_valid = esp_ptr_executable((void *)processed_frame_pc);

            if (!esp_stack_ptr_is_sane(frame.sp) || !frame_pc_is_valid) {
                crashData.callstackCorrupted = true;
                break;
            }
            crashData.callstack[crashData.callstackLength].pc = frame.pc;
#if CRASH_DATA_INCLUDES_SP
            crashData.callstack[crashData.callstackLength].sp = frame.sp;
#endif
            crashData.callstackLength++;
        } else {
            crashData.callstackCorrupted = true;
            break;
        }
    }

    // TODO: Handle corrupted logic

    __real_esp_panic_handler(info);
}

}

const CrashData& getRtcCrashData() { return crashData; }

#elif defined(ESP_PLATFORM)

// Stub implementation for RISC-V and other architectures
// TODO: Implement crash data collection for RISC-V using frame pointer or EH frame

#include <Tactility/kernel/PanicHandler.h>

static CrashData emptyCrashData = {};

const CrashData& getRtcCrashData() { return emptyCrashData; }

#endif