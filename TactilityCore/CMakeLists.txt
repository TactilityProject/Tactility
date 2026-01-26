cmake_minimum_required(VERSION 3.20)

if (DEFINED ENV{ESP_IDF_VERSION})
    file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

    idf_component_register(
        SRCS ${SOURCE_FILES}
        INCLUDE_DIRS "Include/"
        REQUIRES TactilityFreeRtos TactilityKernel mbedtls nvs_flash esp_rom
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unused-variable)
    endif()
else()
    file(GLOB_RECURSE SOURCES "Source/*.c*")

    add_library(TactilityCore OBJECT)

    target_sources(TactilityCore
        PRIVATE ${SOURCES}
    )

    target_include_directories(TactilityCore PUBLIC Include/)

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)

    target_link_libraries(TactilityCore PUBLIC
        TactilityFreeRtos
        TactilityKernel
        mbedtls
    )
endif()