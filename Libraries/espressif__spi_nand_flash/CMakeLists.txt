idf_build_get_property(target IDF_TARGET)

set(reqs)
set(inc include)
set(priv_inc priv_include)
set(srcs "src/nand.c"
         "src/dhara_glue.c"
         "src/nand_impl_wrap.c")

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" GREATER_EQUAL "6.0")
    list(APPEND reqs esp_blockdev)
    if(CONFIG_NAND_FLASH_ENABLE_BDL)
        list(APPEND srcs "src/nand_flash_blockdev.c"
                         "src/nand_wl_blockdev.c")
    endif()
endif()

if(${target} STREQUAL "linux")
    list(APPEND srcs "src/nand_impl_linux.c"
                     "src/nand_linux_mmap_emul.c")
else()
    list(APPEND srcs "src/devices/nand_winbond.c"
                     "src/devices/nand_gigadevice.c"
                     "src/devices/nand_alliance.c"
                     "src/devices/nand_micron.c"
                     "src/devices/nand_zetta.c"
                     "src/devices/nand_xtx.c"
                     "src/devices/nand_fm.c"
                     "src/devices/nand_macronix.c"
                     "src/nand_impl.c"
                     "src/nand_diag_api.c"
                     "src/spi_nand_oper.c")

    set(priv_reqs esp_mm)

    if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER "5.3")
        list(APPEND reqs esp_driver_spi)
    else()
        list(APPEND reqs driver)
    endif()
endif()


idf_component_register(SRCS ${srcs}
        INCLUDE_DIRS ${inc}
        PRIV_INCLUDE_DIRS ${priv_inc}
        REQUIRES ${reqs}
        PRIV_REQUIRES ${priv_reqs})
