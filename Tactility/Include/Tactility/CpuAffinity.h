#pragma once

#include <Tactility/freertoscompat/RTOS.h>

namespace tt {

typedef portBASE_TYPE CpuAffinity;

constexpr static CpuAffinity None = -1;

/**
 * Determines the preferred affinity for certain (sub)systems.
 */
struct CpuAffinityConfiguration {
    CpuAffinity system;
    CpuAffinity graphics; // Display, LVGL
    CpuAffinity wifi;
    CpuAffinity mainDispatcher;
    CpuAffinity apps;
    CpuAffinity timer; // Tactility Timer (based on FreeRTOS)
};

void setCpuAffinityConfiguration(const CpuAffinityConfiguration& config);

const CpuAffinityConfiguration& getCpuAffinityConfiguration();

}
