if (COMMAND tactility_add_module)
    return()
endif()

cmake_minimum_required(VERSION 3.24)

macro(tactility_get_module_name NAME OUT_NAME)
    if (DEFINED ENV{ESP_IDF_VERSION})
        set(${OUT_NAME} ${COMPONENT_LIB})
    else ()
        set(${OUT_NAME} ${NAME})
    endif ()
endmacro()

# The platform name of a build for this computer, e.g. "linux-x86_64" or "macos-arm64".
# It names the folder of an app binary (bin/<platform>) and the SDK platform.
macro(tactility_get_posix_platform OUT_NAME)
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(${OUT_NAME} "macos-${CMAKE_SYSTEM_PROCESSOR}")
    else ()
        set(${OUT_NAME} "linux-${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
endmacro()

macro(tactility_add_module NAME)
    # WHOLE_ARCHIVE: force every object file in this module into the final link unconditionally,
    # instead of only the ones some other already-scanned archive currently has a pending
    # undefined reference to. Needed when this module provides symbols a component it depends on
    # (e.g. lvgl__lvgl's custom-allocator hooks) calls back into - a reverse reference a normal
    # single-pass static-archive link can't resolve, since that component is scanned after this
    # one's archive has already been passed once.
    set(options WHOLE_ARCHIVE)
    set(oneValueArgs)
    set(multiValueArgs SRCS INCLUDE_DIRS PRIV_INCLUDE_DIRS REQUIRES PRIV_REQUIRES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (DEFINED ENV{ESP_IDF_VERSION})
        # idf_component_register's WHOLE_ARCHIVE is a presence-based flag (no value) - only pass
        # the token at all when requested, rather than passing ARG_WHOLE_ARCHIVE's TRUE/FALSE as
        # a value, which idf_component_register doesn't expect.
        set(whole_archive_arg)
        if (ARG_WHOLE_ARCHIVE)
            set(whole_archive_arg WHOLE_ARCHIVE)
        endif()
        idf_component_register(
            SRCS ${ARG_SRCS}
            INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
            PRIV_INCLUDE_DIRS ${ARG_PRIV_INCLUDE_DIRS}
            REQUIRES ${ARG_REQUIRES}
            PRIV_REQUIRES ${ARG_PRIV_REQUIRES}
            ${whole_archive_arg}
        )
    else()
        add_library(${NAME} STATIC)
        target_sources(${NAME} PRIVATE ${ARG_SRCS})
        target_include_directories(${NAME}
            PRIVATE ${ARG_PRIV_INCLUDE_DIRS}
            PUBLIC ${ARG_INCLUDE_DIRS}
        )
        target_link_libraries(${NAME} PUBLIC ${ARG_REQUIRES})
        target_link_libraries(${NAME} PRIVATE ${ARG_PRIV_REQUIRES})
        if (ARG_WHOLE_ARCHIVE)
            # A static archive only pulls in object files that already have a pending undefined
            # reference at the point the archive is scanned, so a plain link drops ${NAME}'s
            # reverse dependencies (see WHOLE_ARCHIVE comment above). Make whoever links ${NAME}
            # whole-archive it instead of just archive-pruning it.
            set_property(TARGET ${NAME} APPEND PROPERTY INTERFACE_LINK_LIBRARIES $<LINK_LIBRARY:WHOLE_ARCHIVE,${NAME}>)
        endif()
    endif()
endmacro()
