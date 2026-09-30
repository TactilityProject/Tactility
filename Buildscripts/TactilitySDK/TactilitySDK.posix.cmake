function(tactility_project)
endfunction()

function(_tactility_project)
endfunction()

macro(tactility_project_pre project_name)
endmacro()

macro(tactility_project_post project_name)
    # The app's own library target is defined in a subdirectory (e.g. "main"); without this it
    # would land nested under that subdirectory instead of directly in the build dir.
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    # Mirrors the ESP-IDF "TactilitySDK" component (Buildscripts/TactilitySDK/CMakeLists.txt):
    # apps link against this single target instead of listing SDK include dirs themselves.
    # Posix apps are dlopen()ed into a running Tactility process (see app-posix-module) and
    # resolve symbols against Tactility's own copies at load time, so headers are all they need
    # at compile time - no libraries to link.
    add_library(TactilitySDK INTERFACE)
    target_include_directories(TactilitySDK INTERFACE
        ${TACTILITY_SDK_PATH}/Modules/app-module/include
        ${TACTILITY_SDK_PATH}/Modules/crypt-module/include
        ${TACTILITY_SDK_PATH}/Modules/gps-module/include
        ${TACTILITY_SDK_PATH}/Modules/lvgl-module/include
        ${TACTILITY_SDK_PATH}/Modules/lvgl-window-manager-module/include
        ${TACTILITY_SDK_PATH}/Modules/service-module/include
        ${TACTILITY_SDK_PATH}/Libraries/TactilityKernel/include
        ${TACTILITY_SDK_PATH}/Libraries/lvgl/include
        ${TACTILITY_SDK_PATH}/Libraries/FreeRTOS-Kernel/include
        ${TACTILITY_SDK_PATH}/Libraries/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix
        ${TACTILITY_SDK_PATH}/Libraries/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils
    )
    target_compile_definitions(TactilitySDK INTERFACE LV_LVGL_H_INCLUDE_SIMPLE)
    # An app's references to its own functions must bind to its own definitions, not to a same-named
    # libc function already loaded in the Tactility process (e.g. an app shipping its own regcomp()).
    # ESP32 apps get the same through -fvisibility=hidden. Mach-O binds within the image by default.
    if (NOT APPLE)
        target_link_options(TactilitySDK INTERFACE -Wl,-Bsymbolic)
    endif ()

    # ESP-IDF's project() auto-discovers the "main" component; plain CMake doesn't.
    add_subdirectory(main)
endmacro()

macro(tactility_component_register)
    cmake_parse_arguments(TT_COMPONENT "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES" ${ARGN})
    # Must be a SHARED object, not a -pie executable: glibc's dlopen() unconditionally refuses
    # any ET_DYN carrying the DF_1_PIE flag ("cannot dynamically load position-independent
    # executable"), regardless of whether it has a dynamic-linker segment - verified empirically.
    add_library(${PROJECT_NAME} SHARED ${TT_COMPONENT_SRCS})
    target_link_libraries(${PROJECT_NAME} PRIVATE TactilitySDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if (TT_COMPONENT_INCLUDE_DIRS)
        target_include_directories(${PROJECT_NAME} PRIVATE ${TT_COMPONENT_INCLUDE_DIRS})
    endif ()
endmacro()
