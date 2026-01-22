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

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCE_FILES} "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c"
        REQUIRES Tactility TactilityC TactilityKernel drivers-esp ${TACTILITY_DEVICE_PROJECT}
    )

else ()

    add_executable(FirmwareSim ${SOURCE_FILES} "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
    target_link_libraries(FirmwareSim
        PRIVATE Tactility
        PRIVATE TactilityCore
        PRIVATE TactilityFreeRtos
        PRIVATE TactilityKernel
        PRIVATE Simulator
        PRIVATE SDL2::SDL2-static SDL2-static
    )

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)
endif ()

file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/Firmware/Generated")

# Generate devicetree code and attach to Firmware component
add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c"
        "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.h"
    COMMAND pip install lark pyyaml
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/devicetree-compiler/compile.py"
    "${DEVICETREE_LOCATION}" "Firmware/Generated"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${DEVICETREE_LOCATION}/devicetree.yaml" # Optional: trigger rebuild if source changes
    COMMENT "Generating devicetree source files..."
)
add_custom_target(Generated DEPENDS "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
set_source_files_properties("${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c" PROPERTIES GENERATED TRUE)
# Update target for generated code
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/Firmware/Generated")
