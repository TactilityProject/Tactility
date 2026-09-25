# Architecture: Hardware Abstraction Layer

## Driver

A driver generally consists of:
- Registration of driver in parent module (optional, but desirable)
- YAML bindings in the `bindings/` folder
- An `#include` that is used in the `.dts` file. The include is in `[projectname]/bindings/[drivername].h`
- The driver implementation: a `.cpp` and `.h` file. The implementation is C++, but the header exposes pure C functions. C implementations are allowed, but C++ is preferred.

Drivers are part of a kernel module.

A driver whose device sits on a shared SPI controller (parent device type `SPI_CONTROLLER_TYPE`)
must bracket its own bus access with `spi_controller_lock()`/`spi_controller_unlock()`
(`spi_controller_lock_bus_of()`/`spi_controller_unlock_bus_of()` for the common case of a direct
child), at the logical-operation level rather than per primitive. This is not done for you by the
kernel's generic device-type wrappers (`display.cpp`, `pointer.cpp`, etc.) - only the driver knows
for certain which of its calls touch the bus.

Modules with drivers can be stored in:
- TactilityKernel
- A subproject in `Platforms` folder
- A subproject in `Devices` folder
- A subproject in `Drivers` folder

## Kernel Modules

Kernel module names are lower case and postfixed with `-module`.

Projects that are kernel modules:

1. Declare a `struct Module`
2. Contain a `module.yaml` file that declares a list of dependencies (for parsing the devicetree) and specifies the bindings folder that contains the drivers' YAML definitions. For example:
```yaml
dependencies:
  - TactilityKernel
bindings: bindings
```
