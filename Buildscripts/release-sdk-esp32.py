#!/usr/bin/env python3

import os
import glob
import subprocess
import sys
import importlib.util
from textwrap import dedent

_shared_spec = importlib.util.spec_from_file_location("release_sdk_shared", os.path.join("Buildscripts", "release-sdk-shared.py"))
shared = importlib.util.module_from_spec(_shared_spec)
_shared_spec.loader.exec_module(shared)

def get_driver_mappings(driver_name):
    return [
        {'src': f'Drivers/{driver_name}/include/**', 'dst': f'Drivers/{driver_name}/include/'},
        {'src': f'Drivers/{driver_name}/*.md', 'dst': f'Drivers/{driver_name}/'},
        {'src': f'build/esp-idf/{driver_name}/lib{driver_name}.a', 'dst': f'Drivers/{driver_name}/binary/lib{driver_name}.a'},
    ]

def get_module_mappings(module_name):
    return [
        {'src': f'Modules/{module_name}/include/**', 'dst': f'Modules/{module_name}/include/'},
        {'src': f'Modules/{module_name}/*.md', 'dst': f'Modules/{module_name}/'},
        {'src': f'build/esp-idf/{module_name}/lib{module_name}.a', 'dst': f'Modules/{module_name}/binary/lib{module_name}.a'},
    ]

def create_module_cmakelists(module_name):
    return dedent(f'''
    cmake_minimum_required(VERSION 3.20)
    idf_component_register(
        INCLUDE_DIRS "include"
    )
    add_prebuilt_library({module_name} "binary/lib{module_name}.a")
    ''')

def driver_is_available(driver_name):
    """
    Some drivers only build for certain chip targets (e.g. sc2356-module is ESP32-P4 only,
    since it depends on esp_video/esp_cam_sensor/PPA which are themselves chip-restricted).
    Build output presence is the single source of truth for "does this driver support the
    current target" - no separate manifest to keep in sync with the real CMakeLists.txt
    REQUIRES/Kconfig guards.
    """
    binary_pattern = f'build/esp-idf/{driver_name}/lib{driver_name}.a'
    return bool(glob.glob(binary_pattern))

def add_driver(target_path, driver_name):
    mappings = get_driver_mappings(driver_name)
    shared.map_copy(mappings, target_path)
    cmakelists_content = create_module_cmakelists(driver_name)
    shared.write_module_cmakelists(os.path.join(target_path, f"Drivers/{driver_name}/CMakeLists.txt"), cmakelists_content)

def add_module(target_path, module_name):
    mappings = get_module_mappings(module_name)
    shared.map_copy(mappings, target_path)
    cmakelists_content = create_module_cmakelists(module_name)
    shared.write_module_cmakelists(os.path.join(target_path, f"Modules/{module_name}/CMakeLists.txt"), cmakelists_content)

def main():
    if len(sys.argv) < 2:
        print("Usage: release-sdk-esp32.py [target_path]")
        print("Example: release-sdk-esp32.py release/TactilitySDK")
        sys.exit(1)

    esp_idf_version = os.environ.get("ESP_IDF_VERSION", "")
    if not esp_idf_version:
        print("Error: ESP_IDF_VERSION environment variable is not set")
        sys.exit(1)

    target_path = os.path.abspath(sys.argv[1])
    os.makedirs(target_path, exist_ok=True)

    # Mapping logic
    mappings = [
        {'src': 'version.txt', 'dst': ''},
        # TactilityFreeRtos
        {'src': 'TactilityFreeRtos/Include/**', 'dst': 'Libraries/TactilityFreeRtos/Include/'},
        {'src': 'TactilityFreeRtos/CMakeLists.txt', 'dst': 'Libraries/TactilityFreeRtos/'},
        {'src': 'TactilityFreeRtos/LICENSE*.*', 'dst': 'Libraries/TactilityFreeRtos/'},
        # TactilityKernel
        {'src': 'build/esp-idf/TactilityKernel/libTactilityKernel.a', 'dst': 'Libraries/TactilityKernel/binary/'},
        {'src': 'TactilityKernel/include/**', 'dst': 'Libraries/TactilityKernel/include/'},
        {'src': 'TactilityKernel/CMakeLists.txt', 'dst': 'Libraries/TactilityKernel/'},
        {'src': 'TactilityKernel/*.md', 'dst': 'Libraries/TactilityKernel/'},
        # lvgl (basics)
        {'src': 'build/esp-idf/lvgl__lvgl/liblvgl__lvgl.a', 'dst': 'Libraries/lvgl/binary/liblvgl.a'},
        {'src': 'Libraries/lvgl/lvgl.h', 'dst': 'Libraries/lvgl/include/'},
        {'src': 'Libraries/lvgl/lv_version.h', 'dst': 'Libraries/lvgl/include/'},
        {'src': 'Libraries/lvgl/LICENCE*.*', 'dst': 'Libraries/lvgl/'},
        {'src': 'Libraries/lvgl/src/lv_conf_kconfig.h', 'dst': 'Libraries/lvgl/include/lv_conf.h'},
        {'src': 'Libraries/lvgl/src/**/*.h', 'dst': 'Libraries/lvgl/include/src/'},
        # elf_loader
        {'src': 'managed_components/espressif__elf_loader/*.cmake', 'dst': 'Libraries/elf_loader/'},
        {'src': 'managed_components/espressif__elf_loader/*.lf', 'dst': 'Libraries/elf_loader/'},
        {'src': 'managed_components/espressif__elf_loader/license.txt', 'dst': 'Libraries/elf_loader/'},
        # minitar
        {'src': 'build/esp-idf/minitar/libminitar.a', 'dst': 'Libraries/minitar/binary/'},
        {'src': 'Libraries/minitar/minitar/minitar.h', 'dst': 'Libraries/minitar/include/'},
        {'src': 'Libraries/minitar/minitar/LICENSE*', 'dst': 'Libraries/minitar/'},
        # minmea
        {'src': 'build/esp-idf/minmea/libminmea.a', 'dst': 'Libraries/minmea/binary/'},
        {'src': 'Libraries/minmea/Include/**', 'dst': 'Libraries/minmea/include/'},
        {'src': 'Libraries/minmea/CMakeLists.txt', 'dst': 'Libraries/minmea/'},
        {'src': 'Libraries/minmea/README.md', 'dst': 'Libraries/minmea/'},
        {'src': 'Libraries/minmea/LICENSE*.*', 'dst': 'Libraries/minmea/'},
        {'src': 'Libraries/minmea/COPYING', 'dst': 'Libraries/minmea/'},
    ]

    shared.map_copy(mappings, target_path)

    # Modules
    module_names = shared.read_module_list(os.path.join('Buildscripts', 'release-sdk-modules.txt'))
    for module_name in module_names:
        add_module(target_path, module_name)

    # Final scripts - copied verbatim
    shared.generate_tactility_sdk_cmake(target_path, 'esp32')
    shared.generate_tactility_sdk_top_cmakelists(target_path)

    # Output ESP-IDF SDK version to file
    with open(os.path.join(target_path, "idf-version.txt"), "w") as f:
        f.write(esp_idf_version)

if __name__ == "__main__":
    main()
