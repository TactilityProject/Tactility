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
        message(FATAL_ERROR "sdkconfig file not found:${PROPERTY_FILE}\nMake sure you select a device by running \"python device.py [device-id]\"\n")
    endif ()
    file(READ ${PROPERTY_FILE_ABS} file_content)
    set(${RESULT_VAR} "${file_content}" PARENT_SCOPE)
endfunction()

