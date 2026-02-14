cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SOURCES "source/*.c**")

add_library(platform-posix OBJECT)
target_sources(platform-posix PRIVATE ${SOURCES})
#target_include_directories(platform-posix PUBLIC include/)
target_link_libraries(platform-posix PUBLIC TactilityKernel)
