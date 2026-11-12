function(tactility_project)
endfunction()

function(_tactility_project)
endfunction()

macro(tactility_project_pre project_name)
    # The SDK installs its own ccache launcher with per-platform settings (see
    # _tactility_enable_sdk_ccache). IDF's own launcher would wrap it a second time
    # (ccache -> cmake -E env -> ccache); on Windows the long command lines then make ccache pass
    # an @response file that CMake misparses. Keep exactly one ccache layer.
    set(CCACHE_ENABLE OFF CACHE BOOL "Enable IDF ccache (disabled: the Tactility SDK has its own launcher)" FORCE)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    set(EXTRA_COMPONENT_DIRS ${TACTILITY_SDK_PATH} ${TACTILITY_SDK_PATH}/Modules)
endmacro()

macro(tactility_project_post project_name)
    set(TACTILITY_SKIP_SPIFFS 1)

    # Tactility's PanicHandler.cpp needs s0 to stay a frame pointer to capture a callstack for
    # a RISC-V app's crashes, which GCC does not guarantee without this flag. The firmware sets the
    # same flag for its own code, but a crash usually happens in app code, built separately here.
    # Gated to RISC-V since Xtensa never reads s0 this way. idf_build_set_property(), not
    # add_compile_options(): the app's code compiles as an idf_component_register() component
    # (Apps/*/main/CMakeLists.txt), which reads ESP-IDF's own COMPILE_OPTIONS build property rather
    # than plain directory-scoped flags. project_elf() below uses the same property for its own
    # flags for the same reason.
    if(CONFIG_IDF_TARGET_ARCH_RISCV)
        idf_build_set_property(COMPILE_OPTIONS "-fno-omit-frame-pointer" APPEND)
    endif()

    # Binds the app's references to its own symbols at link time. The C++ standard library's templates
    # (e.g. std::shared_ptr internals) keep default visibility despite -fvisibility=hidden, so without this
    # the linker leaves symbol relocations for them, which the ELF loader only resolves against the
    # firmware's exported symbols. On RISC-V that fails to load the app with "Can't find common <symbol>".
    # project_elf() links with its own flags plus ELF_LIBS, so the option goes there.
    list(APPEND ELF_LIBS "-Wl,-Bsymbolic")

    # ESP-IDF's own <sys/ioctl.h> (esp_libc/platform_include) is found before app-module's, which adds
    # the terminal ioctls (TIOCGWINSZ) Tactility's ioctl() supports for app fds.
    # C and C++ only: assembly sources can't include a C header.
    idf_build_set_property(C_COMPILE_OPTIONS "SHELL:-include ${TACTILITY_SDK_PATH}/Modules/app-module/include/sys/ioctl.h" APPEND)
    idf_build_set_property(CXX_COMPILE_OPTIONS "SHELL:-include ${TACTILITY_SDK_PATH}/Modules/app-module/include/sys/ioctl.h" APPEND)

    include("${TACTILITY_SDK_PATH}/Libraries/elf_loader/elf_loader.cmake")
    project_elf($project_name)

    file(READ ${TACTILITY_SDK_PATH}/idf-version.txt TACTILITY_SDK_IDF_VERSION)
    string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*" "\\1" TACTILITY_SDK_IDF_MAJOR_MINOR "${TACTILITY_SDK_IDF_VERSION}")
    string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*" "\\1" CURRENT_IDF_MAJOR_MINOR "$ENV{ESP_IDF_VERSION}")
    if (NOT "${CURRENT_IDF_MAJOR_MINOR}" STREQUAL "${TACTILITY_SDK_IDF_MAJOR_MINOR}")
        message(FATAL_ERROR "ESP-IDF version of Tactility SDK (${TACTILITY_SDK_IDF_VERSION}) does not match current ESP-IDF version ($ENV{ESP_IDF_VERSION})")
    endif()

    set(EXTRA_COMPONENT_DIRS
        "${TACTILITY_SDK_PATH}/Libraries/TactilityFreeRtos"
        "${TACTILITY_SDK_PATH}/Modules"
    )

    set(COMPONENTS
        TactilityFreeRtos
        app-module
        crypt-module
        gps-module
        lvgl-module
        lvgl-window-manager-module
        service-module
    )

    _tactility_enable_sdk_ccache()

endmacro()

# Compiles the ESP-IDF and SDK components through ccache, so apps built for the same SDK and
# platform reuse each other's objects: the SDK's fixed sdkconfig makes these objects identical
# across apps. The app's own components get no launcher, so their sources are never cached.
# Each platform uses its own cache directory: <TACTILITY_CCACHE_DIR or ~/.tactility/ccache>/<target>.
# Disable with -D TACTILITY_CCACHE=OFF (the tool's --no-ccache).
function(_tactility_enable_sdk_ccache)
    set(TACTILITY_CCACHE ON CACHE BOOL "Compile ESP-IDF and SDK components through ccache")
    if (NOT TACTILITY_CCACHE)
        return()
    endif()
    find_program(TACTILITY_CCACHE_PROGRAM ccache)
    if (NOT TACTILITY_CCACHE_PROGRAM)
        return()
    endif()

    if (DEFINED ENV{TACTILITY_CCACHE_DIR})
        set(cache_root "$ENV{TACTILITY_CCACHE_DIR}")
    elseif (CMAKE_HOST_WIN32)
        set(cache_root "$ENV{USERPROFILE}/.tactility/ccache")
    else()
        set(cache_root "$ENV{HOME}/.tactility/ccache")
    endif()
    idf_build_get_property(target IDF_TARGET)

    # The settings are part of the launcher, so they also apply when idf.py runs without the tool.
    # They remove the parts of an SDK compile command that differ per app:
    # - CCACHE_BASEDIR: makes paths into the build directory (-I<build>/config) relative
    # - CCACHE_NOHASHDIR: ignores the working directory that -g would otherwise add to the hash
    # - CCACHE_IGNOREOPTIONS: ignores -fmacro-prefix-map=<app directory>=., which can't affect
    #   sources outside the app directory
    set(launcher
        ${CMAKE_COMMAND} -E env
        "CCACHE_DIR=${cache_root}/${target}"
        "CCACHE_BASEDIR=${CMAKE_BINARY_DIR}"
        "CCACHE_NOHASHDIR=1"
        "CCACHE_IGNOREOPTIONS=-fmacro-prefix-map=*"
        ${TACTILITY_CCACHE_PROGRAM}
    )

    # Selected by the directory that defines the target, not by component: some components define
    # extra library targets (e.g. mbedtls's mbedcrypto), and the app's own targets (main, the ELF)
    # are defined in the app directory. CMake has no launcher for assembly, so .S files aren't cached.
    idf_build_get_property(idf_path IDF_PATH)
    set(sdk_roots "${idf_path}" "${TACTILITY_SDK_PATH}")
    set(directories "${CMAKE_SOURCE_DIR}")
    while (directories)
        list(POP_FRONT directories directory)
        get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        list(APPEND directories ${subdirectories})
        set(is_sdk_directory FALSE)
        foreach(sdk_root ${sdk_roots})
            cmake_path(IS_PREFIX sdk_root "${directory}" NORMALIZE is_prefix)
            if (is_prefix)
                set(is_sdk_directory TRUE)
            endif()
        endforeach()
        if (NOT is_sdk_directory)
            continue()
        endif()
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target_name ${targets})
            get_target_property(target_type ${target_name} TYPE)
            if (target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
                set_target_properties(${target_name} PROPERTIES
                    C_COMPILER_LAUNCHER "${launcher}"
                    CXX_COMPILER_LAUNCHER "${launcher}"
                )
            endif()
        endforeach()
    endwhile()
endfunction()

macro(tactility_component_register)
    cmake_parse_arguments(TT_COMPONENT "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES" ${ARGN})
    idf_component_register(
        SRCS ${TT_COMPONENT_SRCS}
        INCLUDE_DIRS ${TT_COMPONENT_INCLUDE_DIRS}
        REQUIRES TactilitySDK ${TT_COMPONENT_REQUIRES}
        PRIV_REQUIRES ${TT_COMPONENT_PRIV_REQUIRES}
    )
endmacro()
