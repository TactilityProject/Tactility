# Architecture: App Framework

Apps are event-driven, C API (`app-module`, `<app/*.h>`), not a C++ class. Each app has an `AppManifest` and a `main(argc, argv)` entry point, like C program's `main()`. Every app instance gets its own dedicated task.

Lifecycle and inter-app communication go through `app_manager_*()` (`app/manager.h`) and `app_event_*()` (`app/event.h`):

Apps are registered at startup via `app_manager_add()`. External apps have a `manifest.properties`, or are side-loaded as binaries (see `app/loader.h`'s `AppLoaderApi`).

Apps can be loaded from:

- memory (`APP_LOCATION_MEMORY`)
- a path pointing to an install folder where an `.app` file was installed (`APP_LOCATION_PATH`)
- a path pointing to an `.elf` or `.so` file (`APP_LOCATION_PATH`)

Apps can build optional UI via the LVGL window-manager module (see `lvgl.md`).
