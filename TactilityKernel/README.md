# TactilityKernel

TactilityKernel is the core component of the Tactility project, providing a hardware abstraction layer (HAL) and essential kernel services for embedded systems.

## Features

- **Device and Driver Model**: A flexible system for managing hardware devices and their corresponding drivers.
- **Peripheral Support**: Standard interfaces for common peripherals:
    - GPIO (General Purpose Input/Output)
    - I2C (Inter-Integrated Circuit)
    - I2S (Inter-IC Sound)
    - SPI (Serial Peripheral Interface)
    - UART (Universal Asynchronous Receiver-Transmitter)
- **Concurrency Primitives**: Built-in support for multi-threaded environments, including:
    - Threads and Dispatchers
    - Mutexes and Recursive Mutexes
    - Event Groups
    - Timers
- **Module System**: Support for loadable modules that can export symbols and provide runtime-extensible functionality.
- **Device Tree Integration**: Utilizes a devicetree-like system for hardware configuration and initialization.
- **Cross-Platform**: Designed to run on both embedded platforms (such as ESP32) and desktop environments (Linux) for simulation and testing.
