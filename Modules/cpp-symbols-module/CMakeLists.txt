cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(cpp-symbols-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel
)

# The module exports out-of-line template instantiations that it has to emit itself. A size optimized
# build inlines them away and the exports then fail to link, so these two files are built without that.
set_source_files_properties(source/module.cpp PROPERTIES COMPILE_OPTIONS "-Og")
set_source_files_properties(source/function_instantiations.cpp PROPERTIES COMPILE_OPTIONS "-O0")
