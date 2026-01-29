cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

# For Generate target below
if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
else ()
    set(TACTILITY_DEVICE_ID simulator)
    set(COMPONENT_LIB FirmwareSim)
endif ()

set(DEVICETREE_LOCATION "${CMAKE_SOURCE_DIR}/Devices/${TACTILITY_DEVICE_ID}")

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
# Ensure the directory is built in the correct CMake build phase
# If the check is not done, then another directory is created in the root of the build folder.
if (DEFINED ${CMAKE_CURRENT_BINARY_DIR})
    file(MAKE_DIRECTORY "${GENERATED_DIR}")
endif ()

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c"
        REQUIRES Tactility TactilityC TactilityKernel PlatformEsp32 ${TACTILITY_DEVICE_PROJECT}
    )

else ()

    add_executable(FirmwareSim ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c")
    target_link_libraries(FirmwareSim PRIVATE
        Tactility
        TactilityCore
        TactilityFreeRtos
        TactilityKernel
        Simulator
        PlatformPosix
        SDL2::SDL2-static SDL2-static
    )

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)
endif ()

add_custom_target(AlwaysRun)
add_custom_command(
    OUTPUT "${GENERATED_DIR}/devicetree.c"
        "${GENERATED_DIR}/devicetree.h"
    COMMAND pip install lark pyyaml
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
