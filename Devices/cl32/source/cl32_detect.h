#pragma once

enum class Cl32HardwareRevision {
    Unknown,
    Revision2,
    Revision3,
    Revision4
};

Cl32HardwareRevision cl32_hardware_revision();

void cl32_power_detect_start();
void cl32_power_detect_stop();
