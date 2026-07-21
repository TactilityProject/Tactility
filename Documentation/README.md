# README

## Project Overview

Tactility is an operating system for the ESP32 microcontroller family. It runs on 40+ supported devices (CYD boards, LilyGO, M5Stack, Elecrow, etc.) and includes a desktop simulator. Built with C++23, ESP-IDF, LVGL, and FreeRTOS.

## Building

### Git

The repository uses git submodules. Make sure to use `--recurse-submodules` on relevant git commands.

### Simulator (Linux/macOS, no ESP-IDF needed)

> [!IMPORTANT]
> The simulator does **NOT** build or run on native Windows (Win32/PowerShell/cmd). This is
> a hard platform limitation, not a missing tool or PATH issue — do not attempt `cmake -B
> buildsim` on Windows, it will not work. WSL is a separate, Linux environment and is fine.

```bash
cmake -B buildsim -G Ninja
ninja -C buildsim              # build firmware + tests
./buildsim/Firmware/Tactility   # run simulator
```

### ESP32 firmware

```bash
python device.py <device-id>   # generate sdkconfig for device (e.g. lilygo-tdeck)
python device.py <device-id> --dev  # dev mode: force 4MB partition table
idf.py build                   # build firmware
idf.py flash monitor            # flash and monitor
```

Device IDs are the folder names under `Devices/` (e.g. `lilygo-tdeck`, `m5stack-cores3`, `cyd-2432s028r`).

#### Windows: activating the ESP-IDF environment

On native Windows, `idf.py` is not on PATH by default — it must be activated per-shell first.
The install script places a PowerShell profile activator per IDF version at
`%IDF_TOOL_PATH%\Microsoft.v<version>.PowerShell_profile.ps1` (path controlled by the
`IDF_TOOL_PATH` environment variable, set to wherever ESP-IDF's tools were installed, e.g.
`C:\Espressif\tools`). Source it before running any `idf.py` command:

```powershell
. "$env:IDF_TOOL_PATH\Microsoft.v5.5.2.PowerShell_profile.ps1"   # match the installed IDF version
Set-Location "<repo-root>"
idf.py build 2>&1 | Select-Object -Last 250
```

This is Windows-specific setup (the main dev works on Linux, where `idf.py` is normally
already on PATH via `export.sh`/`. ./export.sh` or a shell profile).

### Devicetree

A device implementation has a `.dts` file.
The parser at `Buildscripts/DevicetreeCompiler/` converts DTS into C code.
It's called from the `Firmware/` build process.

### Tests

Tests use Doctest and run on simulator (POSIX) target only:

```bash
cmake -B buildsim -G Ninja
ninja -C buildsim build-tests
cd buildsim && ctest            # run all tests
./buildsim/Tests/TactilityKernel/TactilityKernelTests
./buildsim/Tests/Tactility/TactilityTests
./buildsim/Tests/TactilityFreeRtos/TactilityFreeRtosTests
./buildsim/Tests/crypt-module/CryptModuleTests
```

## Architecture

### Layer Stack (bottom to top)

- **TactilityKernel** — C API kernel: device/driver/module lifecycle, concurrency primitives (thread, mutex, timer, dispatcher), filesystem, logging. Header convention: `<tactility/*.h>` (lowercase snake_case).
- **TactilityFreeRtos** — Thin C++ wrappers around FreeRTOS primitives.
- **Tactility** — Main OS layer: app framework, service framework, HAL (deprecated, replaced by TactilityKernel), LVGL integration, networking and services (Wi-Fi, BLE, NTP, ESP-NOW), settings, i18n.
- **TactilityC** — C bindings (`tt_*.h`) for Tactility, used by side-loaded ELF apps on ESP32. Deprecated, replaced by TactilityKernel.
- **Firmware** — Entry point (`app_main`).

### Device/Driver/Module System (kernel layer, C API)

The kernel uses a Linux-inspired device model:

- **Module** (`struct Module`): loadable unit that registers drivers and hardware. Lifecycle: `module_construct` → `module_add` → `module_start`. Each device board and platform is a module.
- **Driver** (`struct Driver`): binds to devices via `compatible` strings (like devicetree). Has `start_device`/`stop_device` callbacks and an `api` pointer for type-specific operations.
- **Device** (`struct Device`): represents hardware. Lifecycle: `device_construct` → `device_add` → `device_start`. Has a parent-child tree, driver binding, and locking.
- **DeviceType** (`struct DeviceType`): enables discovering devices by category (e.g. `DISPLAY_TYPE`, `TOUCH_TYPE`, `UART_CONTROLLER_TYPE`).

Devices are defined via **devicetree** `.dts` files in each `Devices/<id>/` folder. A custom devicetree compiler (`Buildscripts/DevicetreeCompiler/compile.py`) generates C code from these files. Each device folder also has a `devicetree.yaml` specifying dependencies and the `.dts` file.

### App Framework

Apps implement `tt::app::App` (or just provide callbacks). Each app has an `AppManifest` with `appId`, `appName`, `appCategory`, and a factory function `createApp`. Apps are registered at startup in `Tactility.cpp`. External apps can be loaded from SD card via `manifest.properties` files, or side-loaded as ELF binaries on ESP32.

### Service Framework

Services implement `tt::service::Service` with a `ServiceManifest`. Services are long-running background processes (GUI, Wi-Fi, loader, statusbar, GPS, etc.).

### HAL Layer

#### Deprecated HAL

Located in Tactility folder.

`tt::hal::Configuration` is declared per-device board (in `Devices/<id>/Source/Configuration.cpp`). It provides `initBoot` for early hardware setup and `createDevices` to instantiate HAL device wrappers (display, touch, power, keyboard, etc.).

#### Current HAL

Located in TactilityKernel. Based on Linux driver subsystems.

#### Driver

A driver generally consists of:
- Registration of driver in parent module (optional)
- YAML bindings in the `bindings/` folder
- An `#include` that is used in the `.dts` file. The include is in `[projectname]/bindings/[drivername].h`
- The driver implementation: a `.cpp` and `.h` file. The implementation is C++, but the header exposes pure C functions.

Drivers can be stored in:
- TactilityKernel
- A subproject in Platforms/ folder
- A subproject in Devices/ folder
- A subproject in Drivers/ folder. This is a kernel module. Naming is lower case and postfixed with `-module`

#### Kernel Modules

Projects that are kernel modules:

1. Declare a `struct Module`
2. Contain a `devicetree.yaml` file that declares a list of dependencies (for parsing the devicetree) and specifies the bindings folder that contains the drivers' YAML definitions. For example:
```yaml
dependencies:
  - TactilityKernel
bindings: bindings
```

### Platform Abstraction

- `Platforms/platform-esp32/` — ESP-IDF specific implementations
- `Platforms/platform-posix/` — POSIX simulator implementations (SDL for display)

### Build System

The `tactility_add_module()` CMake macro (in `Buildscripts/module.cmake`) wraps ESP-IDF's `idf_component_register` on ESP32 and standard `add_library` on POSIX, allowing the same source to build for both targets.

`device.py` reads `Devices/<id>/device.properties` and generates the `sdkconfig` file with all necessary ESP-IDF config (target chip, flash size, SPIRAM, LVGL fonts, Bluetooth, USB, etc.).

### LVGL

User interfaces should scale well for everything between very large (e.g. 1280x720) and small (e.g. 135x240) displays. Vertical and horizontal layouts are supported.

## Coding Style

Two conventions coexist; which one to use depends on the project layer:

- **C code** (TactilityKernel, drivers): `lower_snake_case` for files, functions, variables. `UpperCamelCase` for types. Files in `source/`, `include/`, `private/` directories.
- **C++ code** (Tactility, apps, services): `UpperCamelCase` for files and types. `lowerCamelCase` for functions. Files in `Source/`, `Include/`, `Private/` directories.

Formatting is enforced by `.clang-format` (LLVM-based, 4-space indent, no column limit).
Never throw exceptions — use return types for error handling. Use `enum class` over plain `enum`.
Don't do null checks: caller is responsible for passing valid data.
Pointers are expected to be non-null unless documented otherwise.

## Key Conventions

- `#ifdef ESP_PLATFORM` guards ESP32-specific code; the simulator uses POSIX equivalents.
- The `Drivers/` directory contains hardware drivers (display controllers, touch controllers, PMICs, etc.) — each is its own CMake component.
- `Modules/` contains cross-cutting modules: `hal-device-module` (device lifecycle) and `lvgl-module` (LVGL task management).
- `Data/system/` and `Data/data/` are flashed as FAT filesystem images on ESP32.
- Translations are in `Translations/` as CSV files, generated via `generate.py`.