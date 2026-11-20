cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

tactility_add_module(uc8179-module
    SRCS source/uc8179.cpp source/module.cpp
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel platform-esp32 driver
)
