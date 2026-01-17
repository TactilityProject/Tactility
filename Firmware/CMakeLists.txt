cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_C_STANDARD 23)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

# Determine device identifier and project location
if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
else ()
    set(TACTILITY_DEVICE_PROJECT "Devices/simulator")
endif ()

set(DEVICETREE_LOCATION "Devices/${TACTILITY_DEVICE_ID}")

if (NOT DEFINED ${COMPONENT_LIB})
    set(COMPONENT_LIB FirmwareSim)
endif ()

if (DEFINED ENV{ESP_IDF_VERSION})

    idf_component_register(
        SRCS ${SOURCE_FILES}
        REQUIRES ${DEVICE_COMPONENTS}
        REQUIRES Tactility TactilityC drivers-core drivers-abstract drivers-esp ${TACTILITY_DEVICE_PROJECT}
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE -std=c23)

else ()

    add_executable(FirmwareSim ${SOURCE_FILES} "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
    target_link_libraries(FirmwareSim
        PRIVATE Tactility
        PRIVATE TactilityCore
        PRIVATE TactilityFreeRtos
        PRIVATE Simulator
        PRIVATE SDL2::SDL2-static SDL2-static
        PRIVATE drivers-abstract
        PRIVATE drivers-core
    )

    target_include_directories(FirmwareSim PRIVATE "${CMAKE_SOURCE_DIR}/Firmware/Generated")
    target_compile_options(FirmwareSim PRIVATE -std=c++23)

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)

    add_dependencies(FirmwareSim Generated)
endif ()

# Generate devicetree code and attach to component
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c ${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.h
    COMMAND mkdir ${CMAKE_SOURCE_DIR}/Firmware/Generated | pip install lark pyyaml && python Buildscripts/devicetree-compiler/compile.py ${DEVICETREE_LOCATION} Firmware/Generated
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)
add_custom_target(Generated DEPENDS ${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c ${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.h)
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "Firmware/Generated/devicetree.c" "/Firmware/Generated/devicetree.h")
# Attach generated code to project
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/Firmware/Generated/devicetree.c")
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_SOURCE_DIR}/Firmware/Generated)
add_dependencies(${COMPONENT_LIB} Generated)
