cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(dummy-i2s-amp-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel audio-codec-module esp_codec_dev
)

# audio_codec_sw_vol.h lives at the esp_codec_dev component root, which its own
# COMPONENT_ADD_INCLUDEDIRS (include interface device/include) does not expose.
if (DEFINED ENV{ESP_IDF_VERSION})
    idf_component_get_property(esp_codec_dev_dir espressif__esp_codec_dev COMPONENT_DIR)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${esp_codec_dev_dir}")
endif()
