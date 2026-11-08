#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include "Tactility/CpuAffinity.h"

namespace tt {

#ifdef ESP_PLATFORM

#ifdef CONFIG_ESP_WIFI_ENABLED

static CpuAffinity getEspWifiAffinity() {
#if CONFIG_FREERTOS_NUMBER_OF_CORES == 1
    return 0;
#elif defined(CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0)
    return 0;
#elif defined(CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1)
    return 1;
#else // Assume core 0 (risky, but safer than "None")
    return 0;
#endif
}

#endif

static CpuAffinity getFreeRtosTimerAffinity() {
#if CONFIG_FREERTOS_NUMBER_OF_CORES == 1
    return 0;
#elif defined(CONFIG_FREERTOS_TIMER_TASK_NO_AFFINITY)
    return None;
#elif defined(CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU0)
    return 0;
#elif defined(CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU1)
    return 1;
#else
    static_assert(false);
#endif
}

#if CONFIG_FREERTOS_NUMBER_OF_CORES == 1
static const CpuAffinityConfiguration esp = {
    .system = 0,
    .graphics = 0,
    .wifi = 0,
    .apps = 0,
    .timer = getFreeRtosTimerAffinity(),
};
#elif CONFIG_FREERTOS_NUMBER_OF_CORES == 2
static const CpuAffinityConfiguration esp = {
    .system = 0,
    .graphics = 1,
#ifdef CONFIG_ESP_WIFI_ENABLED
    .wifi = getEspWifiAffinity(),
#else
    .wifi = 0,
#endif
    .apps = 1,
    .timer = getFreeRtosTimerAffinity(),
};
#endif

#else

static const CpuAffinityConfiguration simulator = {
    .system = None,
    .graphics = None,
    .wifi = None,
    .apps = None,
    .timer = None,
};

#endif

const CpuAffinityConfiguration& getCpuAffinityConfiguration() {
#ifdef ESP_PLATFORM
    return esp;
#else
    return simulator;
#endif
}

}
