cmake_minimum_required(VERSION 3.20)

if (DEFINED ENV{ESP_IDF_VERSION})
    file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

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

    file(GLOB_RECURSE SOURCES "Source/*.c*")

    add_library(hal-device OBJECT)

    target_sources(hal-device PRIVATE ${SOURCES})

    target_include_directories(hal-device
        PRIVATE Private/
        PUBLIC Include/
    )

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)

    target_link_libraries(hal-device PUBLIC
        TactilityFreeRtos
        TactilityCore
        TactilityKernel
        freertos_kernel
    )
    
endif ()

