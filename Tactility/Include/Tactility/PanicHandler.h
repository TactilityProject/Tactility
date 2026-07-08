#ifdef ESP_PLATFORM

#pragma once

#include <cstdio>

#define CRASH_DATA_CALLSTACK_LIMIT 64
#define CRASH_DATA_INCLUDES_SP false

/** Represents a single frame on the callstack. */
struct CallstackFrame {
    uint32_t pc = 0;
#if CRASH_DATA_INCLUDES_SP
    uint32_t sp = 0;
#endif
};

/** Callstack-related crash data. */
struct CrashData {
    bool callstackCorrupted = false;
    uint8_t callstackLength = 0;
    CallstackFrame callstack[CRASH_DATA_CALLSTACK_LIMIT];
};

/** @return the crash data */
const CrashData& getRtcCrashData();

#endif