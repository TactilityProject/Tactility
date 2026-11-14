cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../Buildscripts/module.cmake")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

# Get the project and device id
if (DEFINED ENV{ESP_IDF_VERSION})
    include("../Buildscripts/device.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_DEVICE_PROJECT GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT)
    get_property(TACTILITY_DEVICE_ID GLOBAL PROPERTY TACTILITY_DEVICE_ID)
else ()
    set(TACTILITY_DEVICE_ID simulator)
    set(COMPONENT_LIB Tactility)
    set(TACTILITY_DEVICE_PROJECT Simulator)
endif ()

set(DEVICETREE_LOCATION "${PROJECT_ROOT}/Devices/${TACTILITY_DEVICE_ID}")

list(APPEND REQUIRES_LIST
    # Main projects
    TactilityKernel
    TactilityFreeRtos
    # Modules/*
    lvgl-module
    lvgl-window-manager-module
    app-module
    app-hub-module
    coreutils-module
    binfont-module
    lodepng-module
    c-symbols-module
    cjson-module
    cpp-symbols-module
    crypt-module
    development-module
    freertos-module
    gps-module
    graphics-module
    http-module
    location-module
    mbedtls-module
    mesh-module
    mesh-meshcore-module
    mesh-meshtastic-module
    posix-symbols-module
    pthread-module
    gps-generic-module
    gps-meshtastic-module
    service-module
    bluetooth-module
    wifi-module
    # Libraries/*
    lv_screenshot
    minitar
)

# A device can leave out the audio modules to save flash with "modules.audio=false" in device.properties
set(TT_AUDIO_MODULES_ENABLED TRUE)
if (EXISTS "${DEVICETREE_LOCATION}/device.properties")
    file(READ "${DEVICETREE_LOCATION}/device.properties" device_properties_audio_content)
    # The properties parser accepts spaces around the key and the value
    if (device_properties_audio_content MATCHES "(^|\n)[ \t]*modules\\.audio[ \t]*=[ \t]*false[ \t]*(\r|\n|$)")
        set(TT_AUDIO_MODULES_ENABLED FALSE)
    endif ()
endif ()
if (TT_AUDIO_MODULES_ENABLED)
    list(APPEND REQUIRES_LIST audio-decoder-module)
elseif (EXISTS "${DEVICETREE_LOCATION}/module.yaml")
    file(READ "${DEVICETREE_LOCATION}/module.yaml" device_module_yaml_content)
    if (device_module_yaml_content MATCHES "audio-stream-module")
        message(FATAL_ERROR "${TACTILITY_DEVICE_ID} has modules.audio=false but its module.yaml depends on audio-stream-module")
    endif ()
endif ()

# Check if device has Bluetooth enabled
# Fixes the sdkconfig bluetooth enable options from getting nuked on non-P4+C6 builds when idf build runs
if (DEFINED ENV{ESP_IDF_VERSION})
    file(READ "${DEVICETREE_LOCATION}/device.properties" device_properties_content)
    if (device_properties_content MATCHES "hardware\\.bluetooth=true")
        list(APPEND REQUIRES_LIST bt)
    endif()
endif()

if (DEFINED ENV{ESP_IDF_VERSION})

    list(APPEND REQUIRES_LIST
        app-esp32-module
        platform-esp32
        driver
        QRCode
        esp_http_server
        esp_http_client
        esp-tls
        esp_wifi
        espressif__cjson
        nvs_flash
        spiffs
        vfs
        fatfs
        lwip
        spi_flash
    )

    if ("${IDF_TARGET}" STREQUAL "esp32s3" OR "${IDF_TARGET}" STREQUAL "esp32p4" OR "${IDF_TARGET}" STREQUAL "esp32s31")
        list(APPEND REQUIRES_LIST esp_tinyusb)
    endif ()

else ()

    list(APPEND REQUIRES_LIST
        platform-posix
        app-posix-module
        freertos_kernel
        cJSON
        lvgl
    )

endif ()

#
# Devicetree code generation: DTS compiler python dependencies
#

execute_process(
    COMMAND python -m pip install lark==1.3.1 pyyaml==6.0.3
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

#
# Devicetree code generation: Devicetree dependency collection
#

# REQUIRES_LIST below is computed once, at configure time. Without this, editing
# module.yaml (e.g. adding a driver dependency) doesn't trigger a cmake reconfigure,
# so the new component's include dirs never reach the compiler until a fullclean.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DEVICETREE_LOCATION}/module.yaml"
    "${DEVICETREE_LOCATION}/device.properties" # due to the "hardware.bluetooth = true" and "modules.audio=false" searches in this file from this CMakeLists.txt
)

execute_process(
    COMMAND python "${PROJECT_ROOT}/Buildscripts/DevicetreeCompiler/dependencies.py" "${DEVICETREE_LOCATION}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    OUTPUT_VARIABLE DEVICE_DEPENDENCIES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Tokenize to array of lines
separate_arguments(DEVICE_DEPENDENCIES UNIX_COMMAND "${DEVICE_DEPENDENCIES}")

# Devicetree dependencies go on a separate list, not REQUIRES_LIST itself: the "simulator" device
# component links back against Tactility, so folding it into Tactility's own REQUIRES here would
# create a dependency cycle. They only apply to the Tactility target itself once it's built as the
# final executable (both platforms), never to a library consumer like TactilityTests.
set(TACTILITY_REQUIRES_LIST ${REQUIRES_LIST})
foreach(dts_dependency IN LISTS DEVICE_DEPENDENCIES)
    message("Adding DTS dependency ${dts_dependency}")
    list(APPEND TACTILITY_REQUIRES_LIST ${dts_dependency})
endforeach()

#
# Devicetree code generation: "Generated/" directory creation
#

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
# Ensure the directory is built in the correct CMake build phase
# If the check is not done, then another directory is created in the root of the build folder.
if (DEFINED CMAKE_CURRENT_BINARY_DIR)
    file(MAKE_DIRECTORY "${GENERATED_DIR}")
endif ()

#
# Component
#

if (DEFINED ENV{ESP_IDF_VERSION})
    idf_component_register(
        SRCS ${SOURCE_FILES} "${GENERATED_DIR}/devicetree.c" "${GENERATED_DIR}/generated_assets.c" "${GENERATED_DIR}/generated_fonts.c"
        INCLUDE_DIRS Include/
        PRIV_INCLUDE_DIRS Private/
        REQUIRES ${TACTILITY_REQUIRES_LIST}
    )

    idf_component_optional_requires(PRIVATE bt)

    if (TT_AUDIO_MODULES_ENABLED)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE TT_AUDIO_MODULES_ENABLED)
    endif ()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-unused-variable)
    endif ()

    if (NOT DEFINED TACTILITY_SKIP_SPIFFS)
        # Read-write (only when user data lives on the internal flash partition)
        if (CONFIG_TT_ROOT_PATH STREQUAL "/data")
            # The folder is not in the repository
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Data/data")
            fatfs_create_spiflash_image(data "${CMAKE_CURRENT_SOURCE_DIR}/../Data/data" FLASH_IN_PROJECT PRESERVE_TIME)
        endif ()
    endif ()
else ()
    list(APPEND TACTILITY_REQUIRES_LIST
        SDL2::SDL2-static
        SDL2-static
    )
    add_executable(Tactility ${SOURCE_FILES})
    target_include_directories(Tactility PUBLIC Include/)
    target_include_directories(Tactility PRIVATE Private/)
    if (TT_AUDIO_MODULES_ENABLED)
        target_compile_definitions(Tactility PRIVATE TT_AUDIO_MODULES_ENABLED)
    endif ()
    target_link_libraries(Tactility PRIVATE ${TACTILITY_REQUIRES_LIST})
    # Exports Tactility's own symbols (-rdynamic) so a dlopen()ed app-posix-module app can resolve
    # calls back into it - the OS-native equivalent of app-esp32-module's custom symbol resolver.
    set_target_properties(Tactility PROPERTIES ENABLE_EXPORTS ON)
    # pthread_attr_setstack and read/write/close/printf-family aren't wrapped here: linking
    # platform-posix and app-module already brings those wraps along (see their own
    # CMakeLists.txt files).
endif ()

#
# Devicetree code generation: custom target
#

# A custom target (not add_custom_command(OUTPUT ...)) so this always reruns: the devicetree's
# real inputs span many files (dts, bindings yaml, driver headers) that no fixed DEPENDS list can
# enumerate. BYPRODUCTS still wires a proper dependency for the sources that consume the output.
add_custom_target(Generated ALL
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/DevicetreeCompiler/compile.py"
        "${DEVICETREE_LOCATION}" "${GENERATED_DIR}"
    BYPRODUCTS "${GENERATED_DIR}/devicetree.c" "${GENERATED_DIR}/devicetree.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating devicetree source files..."
)
set_source_files_properties("${GENERATED_DIR}/devicetree.c" PROPERTIES GENERATED TRUE)
set_source_files_properties("${GENERATED_DIR}/devicetree.h" PROPERTIES GENERATED TRUE)
# Update target for generated code
target_sources(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}/devicetree.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}")
add_dependencies(${COMPONENT_LIB} Generated)

#
# Assets: Data/system embedded as C byte arrays
#

# Rewrites its outputs only when the content changes. The assets are the whole Data/system tree,
# so no fixed DEPENDS list is kept. Files in the device's system/ folder replace the ones with the same path.
add_custom_target(GeneratedAssets ALL
    COMMAND python "${CMAKE_SOURCE_DIR}/Buildscripts/generate-assets.py"
        "${CMAKE_SOURCE_DIR}/Data/system" "${GENERATED_DIR}" "${DEVICETREE_LOCATION}/system"
    BYPRODUCTS "${GENERATED_DIR}/generated_assets.c" "${GENERATED_DIR}/generated_assets.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating asset source files..."
)
set_source_files_properties("${GENERATED_DIR}/generated_assets.c" PROPERTIES GENERATED TRUE)
set_source_files_properties("${GENERATED_DIR}/generated_assets.h" PROPERTIES GENERATED TRUE)
target_sources(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}/generated_assets.c")
add_dependencies(${COMPONENT_LIB} GeneratedAssets)

#
# Fonts: the binfont files of the device's default font size, embedded as C byte arrays
#

# The fonts are rasterized by a tool that is built with the host compiler. Without one the table stays empty.
if (DEFINED ENV{ESP_IDF_VERSION})
    set(fonts_simulator 0)
else ()
    set(fonts_simulator 1)
endif ()
set(FONT_GENERATOR "${CMAKE_SOURCE_DIR}/Buildscripts/generate-fonts.py")
# The generator reads the device DTS files, the font assets and the icon/version sources. Track
# them so an incremental build picks up a changed TTF or codepoint list.
file(GLOB DEVICE_FONT_INPUTS CONFIGURE_DEPENDS "${DEVICETREE_LOCATION}/*.dts")
list(APPEND DEVICE_FONT_INPUTS
    "${CMAKE_SOURCE_DIR}/Data/system/fonts/AdwaitaSans.ttf"
    "${CMAKE_SOURCE_DIR}/Data/system/fonts/AdwaitaMono.ttf"
    "${CMAKE_SOURCE_DIR}/Data/system/fonts/MaterialSymbolsRounded.ttf"
    "${CMAKE_SOURCE_DIR}/Data/system/fonts/MaterialSymbolsRounded.codepoints"
    "${CMAKE_SOURCE_DIR}/Tactility/Private/Tactility/lvgl/FontVersions.h"
    "${CMAKE_SOURCE_DIR}/Modules/lvgl-module/source/icon_names.c"
    "${CMAKE_SOURCE_DIR}/Modules/lvgl-module/include/lvgl/icons/names.h"
)
# The generator builds its own host tool from these sources; a change to the renderer must also
# invalidate the embedded font bytes.
file(GLOB_RECURSE BINFONT_TOOL_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Buildscripts/BinfontTool/*.c*"
    "${CMAKE_SOURCE_DIR}/Buildscripts/BinfontTool/*.h"
    "${CMAKE_SOURCE_DIR}/Modules/binfont-module/source/*.c*"
    "${CMAKE_SOURCE_DIR}/Modules/binfont-module/include/*.h"
    "${CMAKE_SOURCE_DIR}/Modules/binfont-module/private/*.h"
    "${CMAKE_SOURCE_DIR}/TactilityKernel/source/memory_posix.cpp"
)
add_custom_command(
    OUTPUT "${GENERATED_DIR}/generated_fonts.c" "${GENERATED_DIR}/generated_fonts.h"
    COMMAND python "${FONT_GENERATOR}"
        "${DEVICETREE_LOCATION}" ${fonts_simulator} "${GENERATED_DIR}"
    DEPENDS "${FONT_GENERATOR}" "${DEVICETREE_LOCATION}/device.properties"
        ${DEVICE_FONT_INPUTS} ${BINFONT_TOOL_INPUTS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating font source files..."
)
add_custom_target(GeneratedFonts ALL
    DEPENDS "${GENERATED_DIR}/generated_fonts.c" "${GENERATED_DIR}/generated_fonts.h"
)
set_source_files_properties("${GENERATED_DIR}/generated_fonts.c" PROPERTIES GENERATED TRUE)
set_source_files_properties("${GENERATED_DIR}/generated_fonts.h" PROPERTIES GENERATED TRUE)
target_sources(${COMPONENT_LIB} PRIVATE "${GENERATED_DIR}/generated_fonts.c")
add_dependencies(${COMPONENT_LIB} GeneratedFonts)

#
# Fonts
#

if (DEFINED ENV{ESP_IDF_VERSION})
    GET_PROPERTY_FILE_CONTENT("${CMAKE_CURRENT_LIST_DIR}/../sdkconfig" sdkconfig_text)
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_FONT_DEFAULT_SIZE" font_default_size)
else ()
    # Default value for simulator
    set(font_default_size 14)
endif ()

message("Default font size: ${font_default_size}")
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    "-DTT_FONT_DEFAULT_SIZE=${font_default_size}"
)
