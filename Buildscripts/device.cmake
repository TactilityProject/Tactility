if (NOT WIN32)
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(Cyan "${Esc}[36m")
else ()
    set(ColorReset "")
    set(Cyan "")
endif ()


include("${CMAKE_CURRENT_LIST_DIR}/properties.cmake")

function(INIT_TACTILITY_GLOBALS SDKCONFIG_FILE)
    GET_PROPERTY_FILE_CONTENT(${SDKCONFIG_FILE} sdkconfig_text)
    # Get device id
    GET_PROPERTY_VALUE(sdkconfig_text "CONFIG_TT_DEVICE_ID" device_id)
    # Validate device id
    if (NOT device_id MATCHES "^[a-z0-9\-]*$")
        message(FATAL_ERROR "Device identifier ${device_id} contains invalid characters. Valid characters: a-z 0-9 \"-\"")
    endif ()
    # Output results
    message("Device identifier: ${Cyan}${device_id}${ColorReset}")
    set(TACTILITY_DEVICE_PROJECT ${device_id})
    message("Device project path: ${Cyan}Devices/${TACTILITY_DEVICE_PROJECT}${ColorReset} ")
    message("")
    set_property(GLOBAL PROPERTY TACTILITY_DEVICE_PROJECT ${TACTILITY_DEVICE_PROJECT})
    set_property(GLOBAL PROPERTY TACTILITY_DEVICE_ID ${device_id})
endfunction()
