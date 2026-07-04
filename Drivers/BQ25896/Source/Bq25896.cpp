#include "Bq25896.h"

#include <tactility/log.h>

constexpr auto* TAG = "BQ25896";

void Bq25896::powerOff() {
    LOG_I(TAG, "Power off");
    bitOn(0x09, BIT(5));
}

void Bq25896::powerOn() {
    LOG_I(TAG, "Power on");
    bitOff(0x09, BIT(5));
}
