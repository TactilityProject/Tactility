cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Get the project and device id
if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
    get_property(TACTILITY_DEVICE_ID GLOBAL PROPERTY TACTILITY_DEVICE_ID)
else ()
    set(TACTILITY_DEVICE_ID simulator)
    set(COMPONENT_LIB FirmwareSim)
    set(TACTILITY_DEVICE_PROJECT Simulator)
endif ()

set(DEVICETREE_LOCATION "${PROJECT_ROOT}/Devices/${TACTILITY_DEVICE_ID}")

# Check if device has Bluetooth enabled
# Fixes the sdkconfig bluetooth enable options from getting nuked on non-P4+C6 builds when idf build runs
if (DEFINED ENV{ESP_IDF_VERSION})
    file(READ "${DEVICETREE_LOCATION}/device.properties" device_properties_content)
    if (device_properties_content MATCHES "hardware\\.bluetooth=true")
        list(APPEND REQUIRES_LIST bt)
    endif()
endif()

#
# DTS compiler python dependencies
#

execute_process(
    COMMAND python -m pip install lark==1.3.1 pyyaml==6.0.3
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

#
# Devicetree dependency collection
#

# REQUIRES_LIST below is computed once, at configure time. Without this, editing
# devicetree.yaml (e.g. adding a driver dependency) doesn't trigger a cmake reconfigure,
# so the new component's include dirs never reach the compiler until a fullclean.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DEVICETREE_LOCATION}/devicetree.yaml"
)

execute_process(
    COMMAND python "${PROJECT_ROOT}/Buildscripts/DevicetreeCompiler/dependencies.py" "${DEVICETREE_LOCATION}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE DEVICE_DEPENDENCIES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Tokenize to array of lines
separate_arguments(DEVICE_DEPENDENCIES UNIX_COMMAND "${DEVICE_DEPENDENCIES}")

#
# "Generated/" directory creation
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

list(APPEND REQUIRES_LIST
    Tactility
    TactilityKernel
)

# Add devicetree dependencies
foreach(dts_dependency IN LISTS DEVICE_DEPENDENCIES)
    message("Adding DTS dependency ${dts_dependency}")
    list(APPEND REQUIRES_LIST ${dts_dependency})
endforeach()

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST
        TactilityC
    )

    idf_component_register(
        SRCS ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c"
        REQUIRES ${REQUIRES_LIST}
    )
else ()
    list(APPEND REQUIRES_LIST
        Tactility
        TactilityFreeRtos
        hal-device-module
        lvgl-module
        crypt-module
        SDL2::SDL2-static
        SDL2-static
    )
    add_executable(FirmwareSim ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c")
    target_link_libraries(FirmwareSim PRIVATE ${REQUIRES_LIST})
endif ()

#
# Devicetree code generation
#

# A plain add_custom_command(OUTPUT ...) only reruns when its explicit DEPENDS (devicetree.yaml)
# changes, since ninja computes dirtiness up front, before any command in this invocation runs.
# The devicetree's real inputs span many files (dts, bindings yaml, driver headers) that a single
# DEPENDS can't enumerate, so this used to be forced to always-rerun via a separate "AlwaysRun"
# custom target that deleted devicetree.c first (DEPENDS on a *target* only creates an order-only
# ninja edge). That delete was invisible to ninja's DAG: on an up-to-date build, ninja would still
# run AlwaysRun (custom targets with no real output are always considered dirty) and delete the
# file, but *not* rerun the generation edge (its own explicit input hadn't changed) or recompile
# devicetree.c.obj (also considered clean) - silently leaving devicetree.c missing on disk until
# some later, unrelated build finally noticed and regenerated it. In between, any build that did
# need to (re)compile devicetree.c.obj hit "No such file or directory".
#
# add_custom_target(... COMMAND ...) has no such problem: unlike add_custom_command(OUTPUT ...),
# a custom target with a COMMAND always reruns on every ninja invocation, so generation and the
# stale artifact removal happen atomically in one edge. BYPRODUCTS tells ninja which files this
# target produces, so it still wires a proper (non-order-only) dependency for the sources that
# consume them.
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
