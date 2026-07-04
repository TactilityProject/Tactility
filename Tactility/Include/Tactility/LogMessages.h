/**
 * Contains common log messages.
 * This helps to keep the binary smaller.
 */
#pragma once

// Crashes
#define LOG_MESSAGE_ILLEGAL_STATE "Illegal state"

// Alloc
#define LOG_MESSAGE_ALLOC_FAILED "Out of memory"
#define LOG_MESSAGE_ALLOC_FAILED_FMT "Out of memory (failed to allocated %d bytes)"

// Mutex
#define LOG_MESSAGE_MUTEX_LOCK_FAILED "Mutex acquisition timeout"
#define LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT "Mutex acquisition timeout (%s)"

// Power on
#define LOG_MESSAGE_POWER_ON_START "Power on"
#define LOG_MESSAGE_POWER_ON_FAILED "Power on failed"
