cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_ASM_COMPILE_OBJECT "${CMAKE_CXX_COMPILER_TARGET}")

include("Buildscripts/logo.cmake")

string(ASCII 27 Esc)
set(ColorReset "${Esc}[m")
set(Cyan "${Esc}[36m")

file(READ version.txt TACTILITY_VERSION)
add_compile_definitions(TT_VERSION="${TACTILITY_VERSION}")

# tactility_add_module() macro
include("Buildscripts/module.cmake")

# Determine device identifier and project location
include("Buildscripts/device.cmake")

if (DEFINED ENV{ESP_IDF_VERSION})
    init_tactility_globals("sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
    get_property(TACTILITY_DEVICE_ID GLOBAL PROPERTY TACTILITY_DEVICE_ID)
else ()
    set(TACTILITY_DEVICE_PROJECT "Devices/simulator")
    set(TACTILITY_DEVICE_ID "simulator")
    set_property(GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT ${TACTILITY_DEVICE_PROJECT})
    set_property(GLOBAL PROPERTY TACTILITY_DEVICE_ID ${TACTILITY_DEVICE_ID})
endif ()

if (DEFINED ENV{ESP_IDF_VERSION})
    message("Using ESP-IDF ${Cyan}v$ENV{ESP_IDF_VERSION}${ColorReset}")
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)

    set(COMPONENTS Tactility)
    set(EXTRA_COMPONENT_DIRS
        # Tactility must be discovered first: its CMakeLists.txt calls init_tactility_globals(),
        # which other components' CMakeLists.txt (e.g. lvgl-module) rely on having already run to
        # read back TACTILITY_DEVICE_ID/TACTILITY_DEVICE_PROJECT via get_property().
        # ESP-IDF's requirements-discovery pass processes EXTRA_COMPONENT_DIRS in an isolated sub-process,
        # in the order listed here, so this must stay first now that Firmware no longer exists as a separate component.
        "Tactility"
        "Devices/${TACTILITY_DEVICE_PROJECT}"
        "Drivers"
        "Modules"
        "Platforms/platform-esp32"
        "TactilityKernel"
        "TactilityKernelCpp"
        "TactilityFreeRtos"
        "Libraries/esp_epaper"
        "Libraries/lv_screenshot"
        "Libraries/minitar"
        "Libraries/minmea"
        "Libraries/QRCode"
    )

    set(EXCLUDE_COMPONENTS "Simulator")

    # panic_info_t is architecture-independent (esp_private/panic_internal.h), wrap applies to every target; PanicHandler.cpp branches internally per architecture.
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)

    # Wraps newlib's reentrant syscall stubs, not the plain read()/write()/close() newlib itself.
    # Implements as thin wrappers around them. newlib's own stdio (fflush()'s buffer-flush path in particular)
    # calls these _r stubs directly, bypassing the plain names entirely.
    # See Modules/app-module/source/stdio_wrap.cpp's own comment for the exact call chain.
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=_read_r" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=_write_r" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=_close_r" APPEND)

    # ioctl()/getcwd()/chdir() have no reentrant _r stubs either.
    # esp_libc's realpath.c and vfs_calls.c define the plain names directly (getcwd()/chdir() as stubs.
    # esp_libc has no real per-process cwd of its own - which is exactly what these wraps replace).
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=ioctl" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=getcwd" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=chdir" APPEND)

    # opendir()/readdir()/closedir() also define the plain names directly (vfs_calls.c).
    # Wrapped so opendir("/") synthesizes a listing of own registered filesystems instead of ENOENT.
    # See Platforms/platform-esp32/source/root_dir.cpp.
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=opendir" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=readdir" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=closedir" APPEND)

    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_button_create" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_dropdown_create" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_list_create" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_list_add_button" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_obj_create" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_obj_set_flex_flow" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_switch_create" APPEND)
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_textarea_create" APPEND)

else ()
    message("Building for sim target")
    # eps-idf generates these from Kconfig, but posix build isn't set up with Kconfig.
    add_compile_definitions(CONFIG_TT_DEVICE_ID="simulator")
    add_compile_definitions(CONFIG_TT_DEVICE_NAME="Simulator")
    add_compile_definitions(CONFIG_TT_DEVICE_VENDOR="")
    add_compile_definitions(CONFIG_TT_DEVICE_NAME_SIMPLE="Simulator")
    add_compile_definitions(CONFIG_TT_LAUNCHER_APP_ID="tactility.launcher")
    add_compile_definitions(CONFIG_TT_AUTO_START_APP_ID="")
    add_compile_definitions(CONFIG_TT_USER_DATA_LOCATION_INTERNAL)
    add_compile_definitions(CONFIG_TT_LVGL_STATUSBAR_COLORS_INVERTED=false)

endif ()

project(Tactility)

if (DEFINED ENV{ESP_IDF_VERSION})
    # PanicHandler.cpp's RISC-V callstack walker requires s0 to stay a frame pointer, which GCC
    # does not guarantee without this flag. CONFIG_ESP_SYSTEM_USE_FRAME_POINTER does not add it
    # (it only selects which ESP-IDF backtrace-printing function gets compiled), and can't be used
    # instead: the bootloader shares this project's sdkconfig with no per-subproject override, and
    # enabling it there overflows the bootloader's fixed partition budget. Setting the flag here via
    # idf_build_set_property() only affects this project's own configure, not the bootloader's
    # separate one, so it's naturally excluded. Gated to RISC-V since Xtensa never reads s0 this way.
    # Must run after project(Tactility) above - project() initializes default build specifications
    # that would otherwise overwrite this.
    if(CONFIG_IDF_TARGET_ARCH_RISCV)
        idf_build_set_property(COMPILE_OPTIONS "-fno-omit-frame-pointer" APPEND)
    endif()
endif ()

# Defined as regular project for PC and component for ESP
if (NOT DEFINED ENV{ESP_IDF_VERSION})
    add_subdirectory(Tactility)
    add_subdirectory(TactilityFreeRtos)
    add_subdirectory(TactilityKernel)
    add_subdirectory(TactilityKernelCpp)
    add_subdirectory(Platforms/platform-posix)
    add_subdirectory(Devices/simulator)
    add_subdirectory(Libraries/cJSON)
    add_subdirectory(Libraries/lv_screenshot)
    add_subdirectory(Libraries/QRCode)
    add_subdirectory(Libraries/minitar)
    add_subdirectory(Libraries/minmea)
    add_subdirectory(Modules/lvgl-module)
    add_subdirectory(Modules/audio-decoder-module)
    add_subdirectory(Modules/c-symbols-module)
    add_subdirectory(Modules/cjson-module)
    add_subdirectory(Modules/cpp-symbols-module)
    add_subdirectory(Modules/crypt-module)
    add_subdirectory(Modules/freertos-module)
    add_subdirectory(Modules/gps-module)
    add_subdirectory(Modules/graphics-module)
    add_subdirectory(Modules/http-module)
    add_subdirectory(Modules/mbedtls-module)
    add_subdirectory(Modules/posix-symbols-module)
    add_subdirectory(Modules/pthread-module)
    add_subdirectory(Modules/service-module)
    add_subdirectory(Modules/app-module)
    add_subdirectory(Modules/coreutils-module)
    add_subdirectory(Modules/app-posix-module)
    add_subdirectory(Modules/lvgl-window-manager-module)
    add_subdirectory(Drivers/gps-generic-module)
    add_subdirectory(Drivers/gps-meshtastic-module)
    # Kernel-only software aggregate; built on sim so its device-lifecycle behavior
    # is covered by Drivers/audio-stream-module/tests.
    add_subdirectory(Drivers/audio-stream-module)

    # FreeRTOS
    set(FREERTOS_CONFIG_FILE_DIRECTORY ${PROJECT_SOURCE_DIR}/Devices/simulator/Source CACHE STRING "")
    set(FREERTOS_PORT GCC_POSIX CACHE STRING "")
    add_subdirectory(Libraries/FreeRTOS-Kernel)
    target_compile_definitions(freertos_kernel PUBLIC "projCOVERAGE_TEST=0")

    # EmbedTLS
    set(ENABLE_TESTING OFF)
    set(ENABLE_PROGRAMS OFF)
    add_subdirectory(Libraries/mbedtls)

    # SDL
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    add_subdirectory(Libraries/SDL) # Added as idf component for ESP and as library for other targets

    # LVGL
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(Libraries/lvgl) # Added as idf component for ESP and as library for other targets
    target_link_libraries(lvgl PRIVATE SDL2-static)

    # Tests
    add_subdirectory(Tests)

endif ()
