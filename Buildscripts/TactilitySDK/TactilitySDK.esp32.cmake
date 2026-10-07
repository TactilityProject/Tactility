function(tactility_project)
endfunction()

function(_tactility_project)
endfunction()

macro(tactility_project_pre project_name)
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
    idf_build_set_property(COMPILE_OPTIONS "-Wl,-Bsymbolic" APPEND)

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

endmacro()

macro(tactility_component_register)
    cmake_parse_arguments(TT_COMPONENT "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES" ${ARGN})
    idf_component_register(
        SRCS ${TT_COMPONENT_SRCS}
        INCLUDE_DIRS ${TT_COMPONENT_INCLUDE_DIRS}
        REQUIRES TactilitySDK ${TT_COMPONENT_REQUIRES}
        PRIV_REQUIRES ${TT_COMPONENT_PRIV_REQUIRES}
    )
endmacro()
