cmake_minimum_required(VERSION 3.20)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if (DEFINED ENV{TACTILITY_SDK_PATH})
    set(TACTILITY_SDK_PATH $ENV{TACTILITY_SDK_PATH})
else()
    set(TACTILITY_SDK_PATH "../../release/TactilitySDK")
    message(WARNING "⚠️ TACTILITY_SDK_PATH environment variable is not set, defaulting to ${TACTILITY_SDK_PATH}")
endif()

include("${TACTILITY_SDK_PATH}/TactilitySDK.cmake")
set(EXTRA_COMPONENT_DIRS ${TACTILITY_SDK_PATH})

project(HelloWorld)
tactility_project(HelloWorld)
