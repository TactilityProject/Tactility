function(tactility_project)
endfunction()

function(_tactility_project)
endfunction()

macro(tactility_project project_name)
    set(TACTILITY_SKIP_SPIFFS 1)

    include("${TACTILITY_SDK_PATH}/Libraries/elf_loader/elf_loader.cmake")
    project_elf($project_name)

    file(READ ${TACTILITY_SDK_PATH}/idf-version.txt TACTILITY_SDK_IDF_VERSION)
    if (NOT "$ENV{ESP_IDF_VERSION}" STREQUAL "${TACTILITY_SDK_IDF_VERSION}")
        message(FATAL_ERROR "ESP-IDF version of Tactility SDK (${TACTILITY_SDK_IDF_VERSION}) does not match current ESP-IDF version ($ENV{ESP_IDF_VERSION})")
    endif()

    set(EXTRA_COMPONENT_DIRS
        "Libraries/TactilityFreeRtos"
        "Modules"
        "Drivers"
    )

    set(COMPONENTS
        TactilityFreeRtos
        bm8563-module
        bm8563-module
        bmi270-module
        mpu6886-module
        pi4ioe5v6408-module
        qmi8658-module
        rx8130ce-module
    )

    # Regular and core features
    add_prebuilt_library(TactilityC Libraries/TactilityC/binary/libTactilityC.a)
    add_prebuilt_library(TactilityKernel Libraries/TactilityKernel/binary/libTactilityKernel.a)
    add_prebuilt_library(lvgl Libraries/lvgl/binary/liblvgl.a)

    target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityC)
    target_link_libraries(${COMPONENT_LIB} INTERFACE TactilityKernel)
    target_link_libraries(${COMPONENT_LIB} INTERFACE lvgl)

endmacro()
