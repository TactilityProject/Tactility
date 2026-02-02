cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "Source/*.c**")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "Include/"
    REQUIRES TactilityKernel driver
)