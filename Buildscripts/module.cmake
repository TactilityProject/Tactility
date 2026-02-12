if (COMMAND tactility_add_module)
    return()
endif()

macro(tactility_get_module_name NAME OUT_NAME)
    if (DEFINED ENV{ESP_IDF_VERSION})
        set(${OUT_NAME} ${COMPONENT_LIB})
    else ()
        set(${OUT_NAME} ${NAME})
    endif ()
endmacro()

macro(tactility_add_module NAME)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SRCS INCLUDE_DIRS PRIV_INCLUDE_DIRS REQUIRES PRIV_REQUIRES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (DEFINED ENV{ESP_IDF_VERSION})
        idf_component_register(
            SRCS ${ARG_SRCS}
            INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
            PRIV_INCLUDE_DIRS ${ARG_PRIV_INCLUDE_DIRS}
            REQUIRES ${ARG_REQUIRES}
            PRIV_REQUIRES ${ARG_PRIV_REQUIRES}
        )
    else()
        add_library(${NAME} OBJECT)
        target_sources(${NAME} PRIVATE ${ARG_SRCS})
        target_include_directories(${NAME}
            PRIVATE ${ARG_PRIV_INCLUDE_DIRS}
            PUBLIC ${ARG_INCLUDE_DIRS}
        )
        target_link_libraries(${NAME} PUBLIC ${ARG_REQUIRES})
        target_link_libraries(${NAME} PRIVATE ${ARG_PRIV_REQUIRES})
    endif()
endmacro()
