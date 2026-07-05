#include <Tactility/kernel/critical/Critical.h>

#include <Tactility/freertoscompat/Task.h>
#include <Tactility/kernel/Kernel.h>

#ifdef ESP_PLATFORM
static portMUX_TYPE critical_mutex;
#define TT_ENTER_CRITICAL() taskENTER_CRITICAL(&critical_mutex)
#define TT_EXIT_CRITICAL() taskEXIT_CRITICAL(&critical_mutex)
#else
#define TT_ENTER_CRITICAL() taskENTER_CRITICAL()
#define TT_EXIT_CRITICAL() taskEXIT_CRITICAL()
#endif

namespace tt::kernel::critical {

CriticalInfo enter() {
    CriticalInfo info = {
        .isrm = 0,
        .fromIsr = (xPortInIsrContext() == pdTRUE),
        .kernelRunning = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    };

    if (info.fromIsr) {
        info.isrm = taskENTER_CRITICAL_FROM_ISR();
    } else if (info.kernelRunning) {
        TT_ENTER_CRITICAL();
    } else {
        portDISABLE_INTERRUPTS();
    }

    return info;
}

void exit(CriticalInfo info) {
    if (info.fromIsr) {
        taskEXIT_CRITICAL_FROM_ISR(info.isrm);
    } else if (info.kernelRunning) {
        TT_EXIT_CRITICAL();
    } else {
        portENABLE_INTERRUPTS();
    }
}

}
