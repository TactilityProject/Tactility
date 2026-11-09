#if defined(ESP_PLATFORM)
#include <sdkconfig.h>
#endif

#if defined(ESP_PLATFORM)

#include <Tactility/PanicHandler.h>

#include <esp_attr.h>
#include <esp_memory_utils.h>
#include <esp_private/panic_internal.h>

#if defined(CONFIG_IDF_TARGET_ARCH_XTENSA)
#include <esp_cpu.h>
#include <esp_cpu_utils.h>
#include <esp_debug_helpers.h>
#include <xtensa/xtruntime.h>
#elif defined(CONFIG_IDF_TARGET_ARCH_RISCV)
#include <riscv/rvruntime-frames.h>

// The walker below reads s0 as a frame pointer. GCC only guarantees this with
// -fno-omit-frame-pointer (set project-wide, excluding the bootloader, in the top-level
// CMakeLists.txt).
#endif

#include <cstring>

extern "C" {

/**
 * This static variable survives a crash reboot.
 * It is reset by the Boot app.
 */
static RTC_NOINIT_ATTR CrashData crashData;

void __real_esp_panic_handler(void* info);

void __wrap_esp_panic_handler(void* info) {

    const auto* panic_info = static_cast<const panic_info_t*>(info);

    switch (panic_info->exception) {
        // Watchdag timer issues are not consider real crashes: they trigger relatively often
        // and could cause a previous real crash to be overwritten by a watchdog timer warning during reboot.
        case PANIC_EXCEPTION_IWDT: crashData.cause = CrashCause::WatchdogInterrupt; return;
        case PANIC_EXCEPTION_TWDT: crashData.cause = CrashCause::WatchdogTask; return;
        // We also don't care about debugger errors:
        case PANIC_EXCEPTION_DEBUG: crashData.cause = CrashCause::Debug; return;
        // We only care about 'real' crashes:
        case PANIC_EXCEPTION_ABORT: crashData.cause = CrashCause::Abort; break;
        case PANIC_EXCEPTION_FAULT:
        default: crashData.cause = CrashCause::Fault; break;
    }

    crashData.callstackLength = 0;
    crashData.callstackCorrupted = false;
    crashData.faultAddress = reinterpret_cast<uint32_t>(panic_info->addr);

    // g_panic_abort_details carries the actual assert()/abort() message when present; panic_info->reason
    // is ESP-IDF's generic description otherwise (e.g. "IllegalInstruction").
    // On RISC-V an abort()/assert() surfaces as an illegal-instruction *fault* (not
    // PANIC_EXCEPTION_ABORT), so also prefer the recorded abort details for faults.
    const char* reason = panic_info->reason;
    if (g_panic_abort_details != nullptr && g_panic_abort_details[0] != '\0') {
        reason = g_panic_abort_details;
    }
    crashData.reason[0] = '\0';
    if (reason != nullptr) {
        strncpy(crashData.reason, reason, sizeof(crashData.reason) - 1);
        crashData.reason[sizeof(crashData.reason) - 1] = '\0';
    }

#if defined(CONFIG_IDF_TARGET_ARCH_XTENSA)
    // Xtensa's register-windowing hardware lets ESP-IDF walk the stack via
    // esp_backtrace_get_start()/esp_backtrace_get_next_frame().
    esp_backtrace_frame_t frame = {
        .pc = 0,
        .sp = 0,
        .next_pc = 0,
        .exc_frame = nullptr
    };

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
#elif defined(CONFIG_IDF_TARGET_ARCH_RISCV)
    // RISC-V has no register-windowing hardware, so the stack has to be walked by hand via the
    // frame-pointer (s0) chain. Algorithm ported from esp-rs/esp-hal's esp-backtrace crate
    // (Apache-2.0): https://github.com/esp-rs/esp-hal/blob/main/esp-backtrace/src/riscv.rs
    //
    // s0 for a frame points just past that frame's saved {ra, s0} pair: the caller's return
    // address is at fp-4, the caller's own frame pointer at fp-8.
    const auto* exc_frame = static_cast<const RvExcFrame*>(panic_info->frame);

    // mepc is the exact faulting instruction, not a return address, so it's used directly rather
    // than read from the stack like the rest of the walk.
    crashData.callstack[0].pc = exc_frame->mepc;
#if CRASH_DATA_INCLUDES_SP
    crashData.callstack[0].sp = exc_frame->sp;
#endif
    crashData.callstackLength++;

    uint32_t fp = exc_frame->s0;

    crashData.callstackCorrupted = !(esp_stack_ptr_is_sane(exc_frame->sp) && esp_ptr_executable(reinterpret_cast<void*>(exc_frame->mepc)));

    while (
        !crashData.callstackCorrupted
        && crashData.callstackLength < CRASH_DATA_CALLSTACK_LIMIT
    ) {
        // esp_stack_ptr_is_sane() also requires 16-byte alignment, a property of sp at call
        // boundaries but not of a frame pointer (fp only needs word alignment). esp_ptr_in_dram()
        // is the same range check without that assumption.
        //
        // Checked against fp-8, not just fp: the record about to be read is [fp-8, fp), and an fp
        // near the very start of the DRAM range can itself pass esp_ptr_in_dram() while fp-8
        // underflows below it, so validate the whole record before dereferencing any of it.
        //
        // Every task's root frame is vPortTaskWrapper() (FreeRTOS-Kernel/portable/riscv/port.c),
        // which marks itself `.cfi_undefined ra`: no valid frame exists below it, so an invalid fp
        // here is the expected end of the walk once at least one real frame has been captured, not
        // corruption. An invalid fp on the very first iteration is a real problem.
        if (!esp_ptr_in_dram(reinterpret_cast<void*>(fp - 8)) || !esp_ptr_in_dram(reinterpret_cast<void*>(fp)) || (fp & 0x3) != 0) {
            crashData.callstackCorrupted = (crashData.callstackLength <= 1);
            break;
        }

        uint32_t ra = *reinterpret_cast<const uint32_t*>(fp - 4);
        uint32_t prev_fp = *reinterpret_cast<const uint32_t*>(fp - 8);

        // A zero return address marks the outermost frame (startup code zero-initialises it).
        if (ra == 0) {
            break;
        }

        if (!esp_ptr_executable(reinterpret_cast<void*>(ra))) {
            crashData.callstackCorrupted = (crashData.callstackLength <= 1);
            break;
        }

        crashData.callstack[crashData.callstackLength].pc = ra;
#if CRASH_DATA_INCLUDES_SP
        crashData.callstack[crashData.callstackLength].sp = fp;
#endif
        crashData.callstackLength++;

        fp = prev_fp;
    }
#endif // CONFIG_IDF_TARGET_ARCH_XTENSA / CONFIG_IDF_TARGET_ARCH_RISCV

    // TODO: Handle corrupted logic

    __real_esp_panic_handler(info);
}

} // extern "C"

bool isRtcCrashDataValid() {
    const auto cause = static_cast<uint8_t>(crashData.cause);
    if (cause == static_cast<uint8_t>(CrashCause::Unknown) ||
        cause > static_cast<uint8_t>(CrashCause::Fault)) {
        return false;
    }
    if (crashData.callstackLength > CRASH_DATA_CALLSTACK_LIMIT) {
        return false;
    }

    // After a full power cycle (e.g. the board's power controller cutting the rails) the RTC
    // no-init area holds random bytes, so the reason must still be a terminated printable string.
    for (size_t i = 0; i < sizeof(crashData.reason); ++i) {
        const auto character = static_cast<unsigned char>(crashData.reason[i]);
        if (character == '\0') {
            return true;
        }
        if (character < 0x20 && character != '\t' && character != '\n') {
            return false;
        }
    }

    // No terminator within the buffer, so the panic handler never wrote this value.
    return false;
}

void resetRtcCrashData() {
    crashData.cause = CrashCause::Unknown;
    crashData.faultAddress = 0;
    crashData.callstackLength = 0;
    crashData.callstackCorrupted = false;
    crashData.reason[0] = '\0';
}

const CrashData& getRtcCrashData() { return crashData; }

#endif
