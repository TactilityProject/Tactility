#!/usr/bin/env python3

import os
import shutil
import glob
import subprocess
import sys
from textwrap import dedent

def map_copy(mappings, target_base):
    """
    Helper function to map input files/directories to output files/directories.
    mappings: list of dicts with 'src' (glob pattern) and 'dst' (relative to target_base or absolute)
    'src' can be a single file or a directory (if it ends with /).
    """
    for mapping in mappings:
        src_pattern = mapping['src']
        dst_rel = mapping['dst']
        dst_path = os.path.join(target_base, dst_rel)

        # To preserve directory structure, we need to know where the wildcard starts
        # or have a way to determine the "base" of the search.
        # We'll split the pattern into a fixed base and a pattern part.
        
        # Simple heuristic: find the first occurrence of '*' or '?'
        wildcard_idx = -1
        for i, char in enumerate(src_pattern):
            if char in '*?':
                wildcard_idx = i
                break
        
        if wildcard_idx != -1:
            # Found a wildcard. The base is the directory containing it.
            pattern_base = os.path.dirname(src_pattern[:wildcard_idx])
        else:
            # No wildcard. If it's a directory, we might want to preserve its name?
            # For now, let's treat no-wildcard as no relative structure needed.
            pattern_base = None

        src_files = glob.glob(src_pattern, recursive=True)
        if not src_files:
            continue

        for src in src_files:
            if os.path.isdir(src):
                continue
            
            if pattern_base and src.startswith(pattern_base):
                # Calculate relative path from the base of the glob pattern
                rel_src = os.path.relpath(src, pattern_base)
                # If dst_rel ends with /, it's a target directory
                if dst_rel.endswith('/') or os.path.isdir(dst_path):
                    final_dst = os.path.join(dst_path, rel_src)
                else:
                    # If dst_rel is a file, we can't really preserve structure 
                    # unless we join it. But usually it's a dir if structure is preserved.
                    final_dst = dst_path
            else:
                final_dst = dst_path if not (dst_rel.endswith('/') or os.path.isdir(dst_path)) else os.path.join(dst_path, os.path.basename(src))

            os.makedirs(os.path.dirname(final_dst), exist_ok=True)
            shutil.copy2(src, final_dst)
            
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
    '''.format(module_name=module_name))

def write_module_cmakelists(path, content):
    with open(path, 'w') as f:
        f.write(content)

def add_driver(target_path, driver_name):
    mappings = get_driver_mappings(driver_name)
    map_copy(mappings, target_path)
    cmakelists_content = create_module_cmakelists(driver_name)
    write_module_cmakelists(os.path.join(target_path, f"Drivers/{driver_name}/CMakeLists.txt"), cmakelists_content)

def add_module(target_path, module_name):
    mappings = get_module_mappings(module_name)
    map_copy(mappings, target_path)
    cmakelists_content = create_module_cmakelists(module_name)
    write_module_cmakelists(os.path.join(target_path, f"Modules/{module_name}/CMakeLists.txt"), cmakelists_content)

def main():
    if len(sys.argv) < 2:
        print("Usage: release-sdk.py [target_path]")
        print("Example: release-sdk.py release/TactilitySDK")
        sys.exit(1)

    target_path = os.path.abspath(sys.argv[1])
    os.makedirs(target_path, exist_ok=True)

    # Mapping logic
    mappings = [
        {'src': 'version.txt', 'dst': ''},
        # TactilityC
        {'src': 'build/esp-idf/TactilityC/libTactilityC.a', 'dst': 'Libraries/TactilityC/binary/'},
        {'src': 'TactilityC/Include/*', 'dst': 'Libraries/TactilityC/include/'},
        {'src': 'TactilityC/CMakeLists.txt', 'dst': 'Libraries/TactilityC/'},
        {'src': 'TactilityC/LICENSE*.*', 'dst': 'Libraries/TactilityC/'},
        # TactilityFreeRtos
        {'src': 'TactilityFreeRtos/Include/**', 'dst': 'Libraries/TactilityFreeRtos/include/'},
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
        {'src': 'Libraries/elf_loader/elf_loader.cmake', 'dst': 'Libraries/elf_loader/'},
        {'src': 'Libraries/elf_loader/license.txt', 'dst': 'Libraries/elf_loader/'},
        # Final scripts
        {'src': 'Buildscripts/TactilitySDK/TactilitySDK.cmake', 'dst': ''},
        {'src': 'Buildscripts/TactilitySDK/CMakeLists.txt', 'dst': ''},
    ]

    map_copy(mappings, target_path)

    # Modules
    add_module(target_path, "lvgl-module")

    # Drivers
    add_driver(target_path, "bm8563-module")
    add_driver(target_path, "bmi270-module")
    add_driver(target_path, "mpu6886-module")
    add_driver(target_path, "pi4ioe5v6408-module")
    add_driver(target_path, "qmi8658-module")
    add_driver(target_path, "rx8130ce-module")

    # Output ESP-IDF SDK version to file
    esp_idf_version = os.environ.get("ESP_IDF_VERSION", "")
    with open(os.path.join(target_path, "idf-version.txt"), "a") as f:
        f.write(esp_idf_version)

if __name__ == "__main__":
    main()
