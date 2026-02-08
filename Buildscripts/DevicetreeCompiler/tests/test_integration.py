import os
import subprocess
import shutil
import sys
import tempfile

# Path to the compile.py script
# We assume this script is in Buildscripts/DevicetreeCompiler/tests/
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
COMPILE_SCRIPT = os.path.join(PROJECT_ROOT, "compile.py")
TEST_DATA_DIR = os.path.join(SCRIPT_DIR, "data")

def run_compiler(config_path, output_path):
    result = subprocess.run(
        [sys.executable, COMPILE_SCRIPT, config_path, output_path],
        capture_output=True,
        text=True,
        cwd=PROJECT_ROOT,
        timeout=60
    )
    return result

def test_compile_success():
    print("Running test_compile_success...")
    with tempfile.TemporaryDirectory() as output_dir:
        result = run_compiler(TEST_DATA_DIR, output_dir)
        
        if result.returncode != 0:
            print(f"FAILED: Compilation failed: {result.stderr}")
            return False
            
        if not os.path.exists(os.path.join(output_dir, "devicetree.c")):
            print("FAILED: devicetree.c not generated")
            return False
            
        if not os.path.exists(os.path.join(output_dir, "devicetree.h")):
            print("FAILED: devicetree.h not generated")
            return False
            
        print("PASSED")
        return True

def test_compile_invalid_dts():
    print("Running test_compile_invalid_dts...")
    with tempfile.TemporaryDirectory() as tmp_dir:
        bad_data_dir = os.path.join(tmp_dir, "bad_data")
        os.makedirs(bad_data_dir)
        output_dir = os.path.join(tmp_dir, "output")
        os.makedirs(output_dir)
        
        with open(os.path.join(bad_data_dir, "devicetree.yaml"), "w") as f:
            f.write("dts: bad.dts\nbindings: bindings")
        
        with open(os.path.join(bad_data_dir, "bad.dts"), "w") as f:
            f.write("/dts-v1/;\n / { invalid syntax }")
            
        os.makedirs(os.path.join(bad_data_dir, "bindings"))
        
        result = run_compiler(bad_data_dir, output_dir)
        
        if result.returncode == 0:
            print("FAILED: Compilation should have failed but succeeded")
            return False
            
        print("PASSED")
        return True

def test_compile_missing_config():
    print("Running test_compile_missing_config...")
    with tempfile.TemporaryDirectory() as output_dir:
        result = run_compiler("/non/existent/path", output_dir)
        
        if result.returncode == 0:
            print("FAILED: Compilation should have failed for non-existent path")
            return False
        
        if "Directory not found" not in result.stdout:
            print(f"FAILED: Expected 'Directory not found' error message, got: {result.stdout}")
            return False
            
        print("PASSED")
        return True

if __name__ == "__main__":
    tests = [
        test_compile_success,
        test_compile_invalid_dts,
        test_compile_missing_config
    ]
    
    failed = 0
    for test in tests:
        if not test():
            failed += 1
            
    if failed > 0:
        print(f"\n{failed} tests failed")
        sys.exit(1)
    else:
        print("\nAll tests passed")
        sys.exit(0)
