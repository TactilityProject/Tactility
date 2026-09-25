# Architecture: Layer Stack (bottom to top)

- **TactilityKernel** — C API kernel: device/driver/module lifecycle, concurrency primitives (thread, mutex, timer, dispatcher), filesystem, logging. Header convention: `<tactility/*.h>` (lowercase snake_case).
- **TactilityKernelCpp** — C++ extensions for TactilityKernel
- **TactilityFreeRtos** — Thin C++ wrappers around FreeRTOS primitives.
- **Tactility** — Main app/firmware project: contains apps, services, LVGL integration, and more.
