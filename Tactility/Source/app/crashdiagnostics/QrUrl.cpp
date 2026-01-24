#ifdef ESP_PLATFORM

#include <Tactility/app/crashdiagnostics/QrUrl.h>
#include <Tactility/kernel/PanicHandler.h>

#include <sstream>
#include <vector>

#if CONFIG_IDF_TARGET_ARCH_XTENSA
#include <esp_cpu_utils.h>
#else
#include <esp_cpu.h>
#endif

#include <sdkconfig.h>

std::string getUrlFromCrashData() {
    auto crash_data = getRtcCrashData();
    std::vector<uint32_t> stack_buffer(crash_data.callstackLength * 2);
    for (int i = 0; i < crash_data.callstackLength; ++i) {
        const CallstackFrame&frame = crash_data.callstack[i];
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        uint32_t pc = esp_cpu_process_stack_pc(frame.pc);
#else
        uint32_t pc = frame.pc;  // No processing needed on RISC-V
#endif
#if CRASH_DATA_INCLUDES_SP
        uint32_t sp = frame.sp;
#endif
        stack_buffer[i * 2] = pc;
#if CRASH_DATA_INCLUDES_SP
        stack_buffer[(i * 2) + 1] = sp;
#endif
    }

    std::stringstream stream;

    stream << "https://oops.tactilityproject.org";
    stream << "?v=" << TT_VERSION; // Version
    stream << "&a=" << CONFIG_IDF_TARGET; // Architecture
    stream << "&b=" << CONFIG_TT_DEVICE_ID; // Board identifier
    stream << "&s="; // Stacktrace

    for (int i = crash_data.callstackLength - 1; i >= 0; --i) {
        uint32_t pc = stack_buffer[(i * 2)];
        stream << std::hex << pc;
#if CRASH_DATA_INCLUDES_SP
        uint32_t sp = stack_buffer[(i * 2) + 1];
        stream << std::hex << sp;
#endif
    }

    return stream.str();
}

#endif
