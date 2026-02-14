cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include/"
    PRIV_INCLUDE_DIRS "private/"
    REQUIRES TactilityKernel driver
)
