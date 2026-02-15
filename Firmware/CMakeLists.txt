cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

# Get the project and device id
if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
else ()
    set(TACTILITY_DEVICE_ID simulator)
    set(COMPONENT_LIB FirmwareSim)
    set(TACTILITY_DEVICE_PROJECT Simulator)
endif ()

set(DEVICETREE_LOCATION "${CMAKE_SOURCE_DIR}/Devices/${TACTILITY_DEVICE_ID}")

# ===============================
# Generated/ directory creation
# ===============================

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
# Ensure the directory is built in the correct CMake build phase
# If the check is not done, then another directory is created in the root of the build folder.
if (DEFINED CMAKE_CURRENT_BINARY_DIR)
    file(MAKE_DIRECTORY "${GENERATED_DIR}")
endif ()

# ===============================
# Component
# ===============================

list(APPEND REQUIRES_LIST
    Tactility
    TactilityKernel
    ${TACTILITY_DEVICE_PROJECT}
)

if (DEFINED ENV{ESP_IDF_VERSION})
    list(APPEND REQUIRES_LIST
        TactilityC
        platform-esp32
    )
    idf_component_register(
        SRCS ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c"
        REQUIRES ${REQUIRES_LIST}
    )
else ()
    list(APPEND REQUIRES_LIST
        TactilityCore
        TactilityFreeRtos
        platform-posix
        hal-device-module
        lvgl-module
        SDL2::SDL2-static
        SDL2-static
    )
    add_executable(FirmwareSim ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c")
    target_link_libraries(FirmwareSim PRIVATE ${REQUIRES_LIST})
endif ()

# ===============================
# Devicetree code generation
# ===============================

add_custom_target(AlwaysRun
    COMMAND ${CMAKE_COMMAND} -E rm -f "${GENERATED_DIR}/devicetree.c"
)
add_custom_command(
    OUTPUT "${GENERATED_DIR}/devicetree.c"
        "${GENERATED_DIR}/devicetree.h"
    COMMAND pip install lark==1.3.1 pyyaml==6.0.3
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/DevicetreeCompiler/compile.py"
        "${DEVICETREE_LOCATION}" "${GENERATED_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS AlwaysRun "${DEVICETREE_LOCATION}/devicetree.yaml" # AlwaysRun ensures it always gets built
    COMMENT "Generating devicetree source files..."
)
add_custom_target(Generated DEPENDS "${GENERATED_DIR}/devicetree.c")
set_source_files_properties("${GENERATED_DIR}/devicetree.c" PROPERTIES GENERATED TRUE)
set_source_files_properties("${GENERATED_DIR}/devicetree.h" PROPERTIES GENERATED TRUE)
# Update target for generated code
target_sources(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}/devicetree.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}")
