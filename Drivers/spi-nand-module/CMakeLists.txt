cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*")

tactility_add_module(spi-nand-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    REQUIRES TactilityKernel esp_driver_spi fatfs esp_timer
    PRIV_REQUIRES espressif__spi_nand_flash espressif__spi_nand_flash_fatfs
)
