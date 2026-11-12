#ifdef ESP_PLATFORM

#pragma once

#include <cstdio>

#define CRASH_DATA_CALLSTACK_LIMIT 64
#define CRASH_DATA_INCLUDES_SP false
#define CRASH_DATA_REASON_LENGTH 128

/** Represents a single frame on the callstack. */
struct CallstackFrame {
    uint32_t pc = 0;
#if CRASH_DATA_INCLUDES_SP
    uint32_t sp = 0;
#endif
};

/** Broad category of what caused the panic (mirrors ESP-IDF's panic_exception_t). */
enum class CrashCause : uint8_t {
    Unknown,
    Debug,
    WatchdogInterrupt,
    WatchdogTask,
    Abort,
    Fault,
};

/** Callstack-related crash data. */
struct CrashData {
    bool callstackCorrupted = false;
    uint8_t callstackLength = 0;
    CallstackFrame callstack[CRASH_DATA_CALLSTACK_LIMIT];

    CrashCause cause = CrashCause::Unknown;
    uint32_t faultAddress = 0;
    char reason[CRASH_DATA_REASON_LENGTH] = { 0 };
};

/** @return the crash data */
const CrashData& getRtcCrashData();

/**
 * @brief Checks whether the RTC crash record contains a real crash.
 *
 * The record lives in RTC no-init memory, which loses its contents (and can contain random
 * bytes) after a full power cycle, e.g. when the board's power controller cuts the rails.
 * Validates the cause and the reason string before the record is reported or rendered.
 */
bool isRtcCrashDataValid();

/** @brief Clears the RTC crash record. */
void resetRtcCrashData();

#endif