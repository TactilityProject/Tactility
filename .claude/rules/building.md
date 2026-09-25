# Building

## Git

The repository uses git submodules. Make sure to use `--recurse-submodules` on relevant git commands.

## Simulator (Linux/macOS, no ESP-IDF needed)

> [!IMPORTANT]
> The simulator does **NOT** build or run on native Windows (Win32/PowerShell/cmd). This is
> a hard platform limitation, not a missing tool or PATH issue — do not attempt `cmake -B
> buildsim` on Windows, it will not work. WSL is a separate, Linux environment and is fine.

```bash
cmake -B buildsim -G Ninja
ninja -C buildsim              # build firmware + tests
./buildsim/Firmware/Tactility   # run simulator
```

## ESP32 firmware

```bash
python device.py <device-id>   # generate sdkconfig for device (e.g. lilygo-tdeck)
python device.py <device-id> --dev  # dev mode: force 4MB partition table
idf.py build                   # build firmware
idf.py flash monitor            # flash and monitor
```

Device IDs are the folder names under `Devices/` (e.g. `lilygo-tdeck`, `m5stack-cores3`, `cyd-2432s028r`).

### Windows: activating the ESP-IDF environment

On native Windows, `idf.py` is not on PATH by default — it must be activated per-shell first.
The install script places a PowerShell profile activator per IDF version at
`%IDF_TOOLS_PATH%\Microsoft.v<version>.PowerShell_profile.ps1` (path controlled by the
`IDF_TOOLS_PATH` environment variable, set to wherever ESP-IDF's tools were installed, e.g.
`C:\Espressif\tools`). Source it before running any `idf.py` command:

```powershell
. "$env:IDF_TOOLS_PATH\Microsoft.v5.5.2.PowerShell_profile.ps1"   # match the installed IDF version
Set-Location "<repo-root>"
idf.py build 2>&1 | Select-Object -Last 250
```

This is Windows-specific setup (the main dev works on Linux, where `idf.py` is normally
already on PATH via `export.sh`/`. ./export.sh` or a shell profile).

## Devicetree

A device implementation has a `.dts` file.
The parser at `Buildscripts/DevicetreeCompiler/` converts DTS into C code.
It's called from the `Tactility/` build process.

## Tests

Tests use Doctest and run on simulator (POSIX) target only:

```bash
cmake -B buildsim -G Ninja
ninja -C buildsim build-tests
cd buildsim && ctest --test-dir Tests
```
