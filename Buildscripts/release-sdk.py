#!/usr/bin/env python3

import os
import shutil
import glob
import subprocess
import sys

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
        {'src': 'build/esp-idf/TactilityC/libTactilityC.a', 'dst': 'Libraries/TactilityC/Binary/'},
        {'src': 'TactilityC/Include/*', 'dst': 'Libraries/TactilityC/Include/'},
        {'src': 'TactilityC/CMakeLists.txt', 'dst': 'Libraries/TactilityC/'},
        {'src': 'TactilityC/LICENSE*.*', 'dst': 'Libraries/TactilityC/'},
        # TactilityFreeRtos
        {'src': 'TactilityFreeRtos/Include/**', 'dst': 'Libraries/TactilityFreeRtos/Include/'},
        {'src': 'TactilityFreeRtos/CMakeLists.txt', 'dst': 'Libraries/TactilityFreeRtos/'},
        {'src': 'TactilityFreeRtos/LICENSE*.*', 'dst': 'Libraries/TactilityFreeRtos/'},
        # TactilityKernel
        {'src': 'build/esp-idf/TactilityKernel/libTactilityKernel.a', 'dst': 'Libraries/TactilityKernel/Binary/'},
        {'src': 'TactilityKernel/Include/**', 'dst': 'Libraries/TactilityKernel/Include/'},
        {'src': 'TactilityKernel/CMakeLists.txt', 'dst': 'Libraries/TactilityKernel/'},
        {'src': 'TactilityKernel/LICENSE*.*', 'dst': 'Libraries/TactilityKernel/'},
        # lvgl-module
        {'src': 'build/esp-idf/lvgl-module/liblvgl-module.a', 'dst': 'Libraries/lvgl-module/Binary/'},
        {'src': 'Modules/lvgl-module/Include/**', 'dst': 'Libraries/lvgl-module/Include/'},
        {'src': 'Modules/lvgl-module/CMakeLists.txt', 'dst': 'Libraries/lvgl-module/'},
        {'src': 'Modules/lvgl-module/LICENSE*.*', 'dst': 'Libraries/lvgl-module/'},
        # lvgl (basics)
        {'src': 'build/esp-idf/lvgl/liblvgl.a', 'dst': 'Libraries/lvgl/Binary/'},
        {'src': 'Libraries/lvgl/lvgl.h', 'dst': 'Libraries/lvgl/Include/'},
        {'src': 'Libraries/lvgl/lv_version.h', 'dst': 'Libraries/lvgl/Include/'},
        {'src': 'Libraries/lvgl/LICENCE.txt', 'dst': 'Libraries/lvgl/LICENSE.txt'},
        {'src': 'Libraries/lvgl/src/lv_conf_kconfig.h', 'dst': 'Libraries/lvgl/Include/lv_conf.h'},
        {'src': 'Libraries/lvgl/src/**/*.h', 'dst': 'Libraries/lvgl/Include/src/'},
        # elf_loader
        {'src': 'Libraries/elf_loader/elf_loader.cmake', 'dst': 'Libraries/elf_loader/'},
        {'src': 'Libraries/elf_loader/license.txt', 'dst': 'Libraries/elf_loader/'},
        # Final scripts
        {'src': 'Buildscripts/TactilitySDK/TactilitySDK.cmake', 'dst': ''},
        {'src': 'Buildscripts/TactilitySDK/CMakeLists.txt', 'dst': ''},
    ]

    map_copy(mappings, target_path)

    # Output ESP-IDF SDK version to file
    esp_idf_version = os.environ.get("ESP_IDF_VERSION", "")
    with open(os.path.join(target_path, "idf-version.txt"), "a") as f:
        f.write(esp_idf_version)

if __name__ == "__main__":
    main()
