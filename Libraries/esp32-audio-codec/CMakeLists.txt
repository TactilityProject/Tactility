cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel esp_codec_dev
)
