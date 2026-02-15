function(GET_PROPERTY_VALUE PROPERTIES_CONTENT_VAR KEY_NAME RESULT_VAR)
    # Search for the key and its value in the properties content
    # Supports KEY=VALUE, KEY="VALUE", and optional spaces around =
    # Use parentheses to capture the value
    # We use ^ and $ with multiline if available, but string(REGEX) doesn't support them easily for lines.
    # So we look for the key at the beginning of the string or after a newline.
    if ("${${PROPERTIES_CONTENT_VAR}}" MATCHES "(^|\n)${KEY_NAME}[ \t]*=[ \t]*\"?([^\n\"]*)\"?")
        set(${RESULT_VAR} "${CMAKE_MATCH_2}" PARENT_SCOPE)
    else ()
        message(FATAL_ERROR "Property '${KEY_NAME}' not found in the properties content.")
    endif ()
endfunction()

function(GET_PROPERTY_FILE_CONTENT PROPERTY_FILE RESULT_VAR)
    get_filename_component(PROPERTY_FILE_ABS ${PROPERTY_FILE} ABSOLUTE)
    # Find the device identifier in the sdkconfig file
    if (NOT EXISTS ${PROPERTY_FILE_ABS})
        message(FATAL_ERROR "Property file not found: ${PROPERTY_FILE}\nMake sure you select a device by running \"python device.py [device-id]\"\n")
    endif ()
    file(READ ${PROPERTY_FILE_ABS} file_content)
    set(${RESULT_VAR} "${file_content}" PARENT_SCOPE)
endfunction()

function(READ_PROPERTIES_TO_MAP PROPERTY_FILE RESULT_VAR)
    get_filename_component(PROPERTY_FILE_ABS ${PROPERTY_FILE} ABSOLUTE)
    if (NOT EXISTS ${PROPERTY_FILE_ABS})
        message(FATAL_ERROR "Property file not found: ${PROPERTY_FILE}")
    endif ()

    file(STRINGS ${PROPERTY_FILE_ABS} lines)
    set(current_section "")
    set(map_content "")

    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if (line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif ()

        if (line MATCHES "^\\[.*\\]$")
            set(current_section "${line}")
        elseif (line MATCHES "^([^=]+)=(.*)$")
            set(key "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")
            string(STRIP "${key}" key)
            string(STRIP "${value}" value)
            # Remove optional quotes from value
            if (value MATCHES "^\"(.*)\"$")
                set(value "${CMAKE_MATCH_1}")
            endif ()

            list(APPEND map_content "${current_section}${key}" "${value}")
        endif ()
    endforeach()

    set(${RESULT_VAR} "${map_content}" PARENT_SCOPE)
endfunction()

function(GET_VALUE_FROM_MAP MAP_VAR KEY_NAME RESULT_VAR)
    list(FIND ${MAP_VAR} "${KEY_NAME}" key_index)
    if (key_index EQUAL -1)
        set(${RESULT_VAR} "" PARENT_SCOPE)
        return()
    endif ()

    math(EXPR value_index "${key_index} + 1")
    list(GET ${MAP_VAR} ${value_index} value)
    set(${RESULT_VAR} "${value}" PARENT_SCOPE)
endfunction()

function(KEY_EXISTS_IN_MAP MAP_VAR KEY_NAME RESULT_VAR)
    list(FIND ${MAP_VAR} "${KEY_NAME}" key_index)
    if (key_index EQUAL -1)
        set(${RESULT_VAR} FALSE PARENT_SCOPE)
    else ()
        set(${RESULT_VAR} TRUE PARENT_SCOPE)
    endif ()
endfunction()

