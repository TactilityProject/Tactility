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
endmacro()
