#!/usr/bin/env python3

import os
import sys
import importlib.util
from textwrap import dedent

_shared_spec = importlib.util.spec_from_file_location("release_sdk_shared", os.path.join("Buildscripts", "release-sdk-shared.py"))
shared = importlib.util.module_from_spec(_shared_spec)
_shared_spec.loader.exec_module(shared)

def get_module_mappings(module_name):
    return [
        {'src': f'Modules/{module_name}/include/**', 'dst': f'Modules/{module_name}/include/'},
        {'src': f'Modules/{module_name}/*.md', 'dst': f'Modules/{module_name}/'},
        {'src': f'buildsim/Modules/{module_name}/lib{module_name}.a', 'dst': f'Modules/{module_name}/binary/lib{module_name}.a'},
    ]

def create_module_cmakelists(module_name):
    return dedent(f'''
    cmake_minimum_required(VERSION 3.20)
    add_library({module_name} STATIC IMPORTED)
    set_target_properties({module_name} PROPERTIES
        IMPORTED_LOCATION "${{CMAKE_CURRENT_LIST_DIR}}/binary/lib{module_name}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${{CMAKE_CURRENT_LIST_DIR}}/include"
    )
    ''')

def add_module(target_path, module_name):
    mappings = get_module_mappings(module_name)
    shared.map_copy(mappings, target_path)
    cmakelists_content = create_module_cmakelists(module_name)
    shared.write_module_cmakelists(os.path.join(target_path, f"Modules/{module_name}/CMakeLists.txt"), cmakelists_content)

def main():
    if len(sys.argv) < 2:
        print("Usage: release-sdk-posix.py [target_path]")
        print("Example: release-sdk-posix.py release/TactilitySDK")
        sys.exit(1)

    esp_idf_version = os.environ.get("ESP_IDF_VERSION", "")
    if esp_idf_version:
        print("Error: ESP_IDF_VERSION environment variable is set - this script packages the POSIX/simulator build, run it outside an ESP-IDF environment")
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
        {'src': 'buildsim/TactilityKernel/libTactilityKernel.a', 'dst': 'Libraries/TactilityKernel/binary/'},
        {'src': 'TactilityKernel/include/**', 'dst': 'Libraries/TactilityKernel/include/'},
        {'src': 'TactilityKernel/CMakeLists.txt', 'dst': 'Libraries/TactilityKernel/'},
        {'src': 'TactilityKernel/*.md', 'dst': 'Libraries/TactilityKernel/'},
        # FreeRTOS-Kernel - TactilityKernel's public headers (tactility/freertos/*.h) include the
        # real FreeRTOS.h/task.h/etc directly, unlike ESP32 where ESP-IDF's own "freertos"
        # component and Kconfig-generated FreeRTOSConfig.h are already part of every project.
        {'src': 'Libraries/FreeRTOS-Kernel/include/**', 'dst': 'Libraries/FreeRTOS-Kernel/include/'},
        {'src': 'Libraries/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/*.h', 'dst': 'Libraries/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/'},
        {'src': 'Libraries/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils/*.h', 'dst': 'Libraries/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils/'},
        {'src': 'Libraries/FreeRTOS-Kernel/LICENSE*.*', 'dst': 'Libraries/FreeRTOS-Kernel/'},
        {'src': 'Devices/simulator/Source/FreeRTOSConfig.h', 'dst': 'Libraries/FreeRTOS-Kernel/include/'},
        # lvgl (basics)
        {'src': 'buildsim/Libraries/lvgl/lib/liblvgl.a', 'dst': 'Libraries/lvgl/binary/liblvgl.a'},
        {'src': 'Libraries/lvgl/lvgl.h', 'dst': 'Libraries/lvgl/include/'},
        {'src': 'Libraries/lvgl/lvgl_private.h', 'dst': 'Libraries/lvgl/include/'},
        {'src': 'Libraries/lvgl/include/**/*.h', 'dst': 'Libraries/lvgl/include/include/'},
        {'src': 'Libraries/lvgl/LICENCE*.*', 'dst': 'Libraries/lvgl/'},
        {'src': 'lv_conf.h', 'dst': 'Libraries/lvgl/include/'},
        {'src': 'Libraries/lvgl/src/**/*.h', 'dst': 'Libraries/lvgl/include/src/'},
        # minitar
        {'src': 'buildsim/Libraries/minitar/libminitar.a', 'dst': 'Libraries/minitar/binary/'},
        {'src': 'Libraries/minitar/minitar/minitar.h', 'dst': 'Libraries/minitar/include/'},
        {'src': 'Libraries/minitar/minitar/LICENSE*', 'dst': 'Libraries/minitar/'},
        # minmea
        {'src': 'buildsim/Libraries/minmea/libminmea.a', 'dst': 'Libraries/minmea/binary/'},
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
    shared.generate_tactility_sdk_cmake(target_path, 'posix')
    shared.generate_tactility_sdk_top_cmakelists(target_path)

if __name__ == "__main__":
    main()
