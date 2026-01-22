cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

if (NOT DEFINED ENV{ESP_IDF_VERSION})
    set(COMPONENT_LIB FirmwareSim)
endif ()

if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)

    idf_component_register(
        SRCS ${SOURCE_FILES}
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

    target_include_directories(FirmwareSim PRIVATE "${CMAKE_SOURCE_DIR}/Firmware/Generated")

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)
endif ()

# Generate devicetree code and attach to Firmware component
add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c"
        "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.h"
    COMMAND mkdir -p "${CMAKE_SOURCE_DIR}/Firmware/Generated"
    COMMAND pip install lark pyyaml
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/devicetree-compiler/compile.py"
    "${DEVICETREE_LOCATION}" "Firmware/Generated"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${DEVICETREE_LOCATION}/devicetree.yaml" # Optional: trigger rebuild if source changes
    COMMENT "Generating devicetree source files..."
)
set_source_files_properties("${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c" PROPERTIES GENERATED TRUE)
add_custom_target(Generated DEPENDS "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
