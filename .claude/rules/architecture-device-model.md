# Architecture: Device/Driver/Module System (kernel layer, C API)

The kernel uses a Linux-inspired device model:

- **Module** (`struct Module`): loadable unit that registers drivers, hardware and symbols. Lifecycle: `module_construct` → `module_add` → `module_start`. Each device board and platform is a module.
- **Driver** (`struct Driver`): binds to devices via `compatible` strings (like devicetree). Has `start_device`/`stop_device` callbacks and an `api` pointer for type-specific operations.
- **Device** (`struct Device`): represents hardware. Lifecycle: `device_construct` → `device_add` → `device_start`. Has a parent-child tree, driver binding, and locking.
- **DeviceType** (`struct DeviceType`): enables discovering devices by category (e.g. `DISPLAY_TYPE`, `TOUCH_TYPE`, `UART_CONTROLLER_TYPE`).

Devices are defined via **devicetree** `.dts` files in each `Devices/<id>/` folder. A custom devicetree compiler (`Buildscripts/DevicetreeCompiler/compile.py`) generates C code from these files. Each device folder also has a `module.yaml` specifying dependencies and the `.dts` file.
