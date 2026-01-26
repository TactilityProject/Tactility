# Get Started

See [https://docs.tactilityproject.org]

# Useful Parameters

You can copy these into `sdkconfig` manually or set them via `idf.py menuconfig`

## LVGL FPS Counter

```properties
CONFIG_LV_USE_OBSERVER=y
CONFIG_LV_USE_PERF_MONITOR=y
```

## LVGL Layer debugging

```properties
CONFIG_LV_USE_REFR_DEBUG=y
CONFIG_LV_USE_LAYER_DEBUG=y
```

## Halt on error

```properties
CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y
# CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT is not set
```

## Stack debugging

[Stack smashing protection mode:](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/kconfig-reference.html#config-compiler-stack-check-mode)

> - In NORMAL mode (GCC flag: -fstack-protector) only functions that call alloca, and functions with buffers larger than 8 bytes are protected.
> - STRONG mode (GCC flag: -fstack-protector-strong) is like NORMAL, but includes additional functions to be protected -- those that have local array definitions, or have references to local frame addresses.
> - In OVERALL mode (GCC flag: -fstack-protector-all) all functions are protected.

```properties
CONFIG_STACK_CHECK_STRONG=y
```

or:

```properties
CONFIG_STACK_CHECK_ALL=y
```
