cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/module.cmake")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

# Get the project and device id
if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
    get_property(TACTILITY_DEVICE_ID GLOBAL PROPERTY TACTILITY_DEVICE_ID)
else ()
    set(TACTILITY_DEVICE_ID simulator)
    set(COMPONENT_LIB Tactility)
    set(TACTILITY_DEVICE_PROJECT Simulator)
endif ()

set(DEVICETREE_LOCATION "${PROJECT_ROOT}/Devices/${TACTILITY_DEVICE_ID}")

list(APPEND REQUIRES_LIST
    # Main projects
    TactilityKernel
    TactilityKernelCpp
    TactilityFreeRtos
    # Modules/*
    lvgl-module
    lvgl-window-manager-module
    app-module
    c-symbols-module
    cpp-symbols-module
    crypt-module
    freertos-module
    gps-module
    http-module
    mbedtls-module
    posix-symbols-module
    pthread-module
    gps-generic-module
    gps-meshtastic-module
    service-module
    # Libraries/*
    lv_screenshot
    minitar
)

# Check if device has Bluetooth enabled
# Fixes the sdkconfig bluetooth enable options from getting nuked on non-P4+C6 builds when idf build runs
if (DEFINED ENV{ESP_IDF_VERSION})
    file(READ "${DEVICETREE_LOCATION}/device.properties" device_properties_content)
    if (device_properties_content MATCHES "hardware\\.bluetooth=true")
        list(APPEND REQUIRES_LIST bt)
    endif()
endif()

if (DEFINED ENV{ESP_IDF_VERSION})

    list(APPEND REQUIRES_LIST
        app-esp32-module
        platform-esp32
        driver
        QRCode
        esp_http_server
        esp_http_client
        esp-tls
        esp_wifi
        espressif__cjson
        nvs_flash
        spiffs
        vfs
        fatfs
        lwip
        spi_flash
    )

    if ("${IDF_TARGET}" STREQUAL "esp32s3" OR "${IDF_TARGET}" STREQUAL "esp32p4")
        list(APPEND REQUIRES_LIST esp_tinyusb)
    endif ()

    if ("${IDF_TARGET}" STREQUAL "esp32p4")
        # EspNowBackendHosted.cpp needs esp_hosted.h / esp_hosted_misc.h.
        list(APPEND REQUIRES_LIST espressif__esp_hosted)
    endif ()

else ()

    list(APPEND REQUIRES_LIST
        platform-posix
        app-posix-module
        freertos_kernel
        cJSON
        lvgl
    )

endif ()

#
# Devicetree code generation: DTS compiler python dependencies
#

execute_process(
    COMMAND python -m pip install lark==1.3.1 pyyaml==6.0.3
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

#
# Devicetree code generation: Devicetree dependency collection
#

# REQUIRES_LIST below is computed once, at configure time. Without this, editing
# devicetree.yaml (e.g. adding a driver dependency) doesn't trigger a cmake reconfigure,
# so the new component's include dirs never reach the compiler until a fullclean.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DEVICETREE_LOCATION}/devicetree.yaml"
    "${DEVICETREE_LOCATION}/device.properties" # due to the "hardware.bluetooth = true" search in this file from this CMakeLists.txt
)

execute_process(
    COMMAND python "${PROJECT_ROOT}/Buildscripts/DevicetreeCompiler/dependencies.py" "${DEVICETREE_LOCATION}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE DEVICE_DEPENDENCIES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Tokenize to array of lines
separate_arguments(DEVICE_DEPENDENCIES UNIX_COMMAND "${DEVICE_DEPENDENCIES}")

# Devicetree dependencies go on a separate list, not REQUIRES_LIST itself: the "simulator" device
# component links back against Tactility, so folding it into Tactility's own REQUIRES here would
# create a dependency cycle. They only apply to the Tactility target itself once it's built as the
# final executable (both platforms), never to a library consumer like TactilityTests.
set(TACTILITY_REQUIRES_LIST ${REQUIRES_LIST})
foreach(dts_dependency IN LISTS DEVICE_DEPENDENCIES)
    message("Adding DTS dependency ${dts_dependency}")
    list(APPEND TACTILITY_REQUIRES_LIST ${dts_dependency})
endforeach()

#
# Devicetree code generation: "Generated/" directory creation
#

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
# Ensure the directory is built in the correct CMake build phase
# If the check is not done, then another directory is created in the root of the build folder.
if (DEFINED CMAKE_CURRENT_BINARY_DIR)
    file(MAKE_DIRECTORY "${GENERATED_DIR}")
endif ()

#
# Component
#

if (DEFINED ENV{ESP_IDF_VERSION})
    idf_component_register(
        SRCS ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c"
        INCLUDE_DIRS Include/
        PRIV_INCLUDE_DIRS Private/
        REQUIRES ${TACTILITY_REQUIRES_LIST}
    )

    idf_component_optional_requires(PRIVATE bt)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unused-variable)
    endif ()

    if (NOT DEFINED TACTILITY_SKIP_SPIFFS)
        # Read-only
        fatfs_create_rawflash_image(system "${CMAKE_CURRENT_SOURCE_DIR}/../Data/system" FLASH_IN_PROJECT PRESERVE_TIME)
        # Read-write (skipped when user data lives on the SD card instead of internal flash)
        if (NOT CONFIG_TT_USER_DATA_LOCATION_SD)
            fatfs_create_spiflash_image(data "${CMAKE_CURRENT_SOURCE_DIR}/../Data/data" FLASH_IN_PROJECT PRESERVE_TIME)
        endif ()
    endif ()
else ()
    list(APPEND TACTILITY_REQUIRES_LIST
        SDL2::SDL2-static
        SDL2-static
    )
    add_executable(Tactility ${SOURCE_FILES})
    target_include_directories(Tactility PUBLIC Include/)
    target_include_directories(Tactility PRIVATE Private/)
    target_link_libraries(Tactility PRIVATE ${TACTILITY_REQUIRES_LIST})
    # Exports Tactility's own symbols (-rdynamic) so a dlopen()ed app-posix-module app can resolve
    # calls back into it - the OS-native equivalent of app-esp32-module's custom symbol resolver.
    set_target_properties(Tactility PROPERTIES ENABLE_EXPORTS ON)
    # Routes every pthread_attr_setstack() call (FreeRTOS's POSIX port hands each task a stack
    # carved out of its own heap through this) to __wrap_pthread_attr_setstack() in
    # Platforms/platform-posix/source/pthread_stack_wrap.c, which no-ops it - see that file for why.
    # --wrap is a GNU ld option; Apple's linker doesn't support it.
    if (NOT APPLE)
        target_link_options(Tactility PRIVATE "-Wl,--wrap=pthread_attr_setstack")
        # Routes every read()/write()/close() call to Tactility/Source/AppStdioWrap.cpp's
        # __wrap_read/write/close(), which forward into app_io_read/write/close() - the fd-table
        # dispatch that lets an app's own stdio (e.g. a fileselection dialog's printf'd result
        # path) reach an AppStream a parent bound via app_manager_start_with_streams(). Mirrors
        # what ESP-IDF's build already does for the ESP32 target (see top-level CMakeLists.txt).
        target_link_options(Tactility PRIVATE "-Wl,--wrap=read" "-Wl,--wrap=write" "-Wl,--wrap=close")
        # glibc's printf/fprintf/etc don't call the public write() symbol internally (they're
        # already compiled into libc.so, out of --wrap's reach), so the read/write/close wrap
        # above can't see them. Newlib (ESP-IDF) doesn't have this gap - its stdio does call the
        # wrappable syscall stubs - so this block is POSIX-only. Wrapping these symbols instead
        # redirects OUR OWN calls to them (the only ones --wrap can rewrite) through
        # AppStdioWrap.cpp's __wrap_* functions, which check the target stream (stdout/stdin) and
        # fall back to the real libc function for any other FILE*.
        target_link_options(Tactility PRIVATE
            "-Wl,--wrap=printf" "-Wl,--wrap=fprintf" "-Wl,--wrap=vprintf" "-Wl,--wrap=vfprintf"
            "-Wl,--wrap=puts" "-Wl,--wrap=fputs" "-Wl,--wrap=putchar" "-Wl,--wrap=fputc"
            "-Wl,--wrap=getchar" "-Wl,--wrap=fgetc" "-Wl,--wrap=fgets"
        )
    endif ()
endif ()

#
# Devicetree code generation: custom target
#

# A custom target (not add_custom_command(OUTPUT ...)) so this always reruns: the devicetree's
# real inputs span many files (dts, bindings yaml, driver headers) that no fixed DEPENDS list can
# enumerate. BYPRODUCTS still wires a proper dependency for the sources that consume the output.
add_custom_target(Generated ALL
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/DevicetreeCompiler/compile.py"
        "${DEVICETREE_LOCATION}" "${GENERATED_DIR}"
    BYPRODUCTS "${GENERATED_DIR}/devicetree.c" "${GENERATED_DIR}/devicetree.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating devicetree source files..."
)
set_source_files_properties("${GENERATED_DIR}/devicetree.c" PROPERTIES GENERATED TRUE)
set_source_files_properties("${GENERATED_DIR}/devicetree.h" PROPERTIES GENERATED TRUE)
# Update target for generated code
target_sources(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}/devicetree.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}")
add_dependencies(${COMPONENT_LIB} Generated)
