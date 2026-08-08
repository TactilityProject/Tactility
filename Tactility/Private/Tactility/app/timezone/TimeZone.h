#pragma once

#include <cstdint>
#include <string>

namespace tt::app::timezone {

/**
 * @return the started dialog's instance id; result (0 = Ok, 1 = Cancelled) is delivered to
 * @a callerAppInstanceId as APP_EVENT_RESULT once the user picks a time zone - call
 * getLastName()/getLastCode() right after receiving it, on result == 0.
 */
uint32_t start(uint32_t callerAppInstanceId, bool saveTimeZone = false);

/** @return the name/code from the last time zone picked by any TimeZone dialog instance. Only
 * one dialog is expected to be open at a time. */
std::string getLastName();
std::string getLastCode();

}
