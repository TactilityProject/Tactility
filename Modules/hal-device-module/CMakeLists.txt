cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "Source/*.c*")

if (DEFINED ENV{ESP_IDF_VERSION})

    list(APPEND REQUIRES_LIST
        TactilityKernel
        TactilityCore
        TactilityFreeRtos
    )

    idf_component_register(
        SRCS ${SOURCE_FILES}
        INCLUDE_DIRS "Include/"
        REQUIRES ${REQUIRES_LIST}
    )

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unused-variable)
    endif ()

else ()

    add_library(hal-device-module OBJECT)

    target_sources(hal-device-module PRIVATE ${SOURCE_FILES})

    target_include_directories(hal-device-module
        PUBLIC Include/
    )

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)

    target_link_libraries(hal-device-module PUBLIC
        TactilityFreeRtos
        TactilityCore
        TactilityKernel
        freertos_kernel
    )
    
endif ()

