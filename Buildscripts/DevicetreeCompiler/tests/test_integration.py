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
            print(f"FAILED: Compilation failed: {result.stderr} {result.stdout}")
            return False
            
        for filename in ["devicetree.c", "devicetree.h"]:
            generated_path = os.path.join(output_dir, filename)
            expected_path = os.path.join(TEST_DATA_DIR, f"expected_{filename}")
            
            if not os.path.exists(generated_path):
                print(f"FAILED: {filename} not generated")
                return False
                
            if not os.path.exists(expected_path):
                print(f"FAILED: {os.path.basename(expected_path)} not found in test data")
                return False

            diff_result = subprocess.run(["diff", "-u", expected_path, generated_path], capture_output=True, text=True)
            if diff_result.returncode != 0:
                print(f"FAILED: {filename} does not match expected_{filename}")
                print(diff_result.stdout)
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

def write_minmax_config(tmp_dir, device_property_line, binding_min=0, binding_max=3, binding_default=1):
    config_dir = os.path.join(tmp_dir, "minmax_data")
    bindings_dir = os.path.join(config_dir, "bindings")
    os.makedirs(bindings_dir)

    with open(os.path.join(config_dir, "devicetree.yaml"), "w") as f:
        f.write("dts: test.dts\nbindings: bindings")

    with open(os.path.join(config_dir, "test.dts"), "w") as f:
        f.write(f"""/dts-v1/;

/ {{
    compatible = "test,root";
    model = "Test Model";

    test-device@0 {{
        compatible = "test,minmax-device";
        {device_property_line}
    }};
}};
""")

    with open(os.path.join(bindings_dir, "test,root.yaml"), "w") as f:
        f.write("description: Test root binding\ncompatible: \"test,root\"\nproperties:\n  model:\n    type: string\n")

    with open(os.path.join(bindings_dir, "test,minmax-device.yaml"), "w") as f:
        f.write(f"""description: Test min/max binding
compatible: "test,minmax-device"
properties:
  ranged-prop:
    type: int
    default: {binding_default}
    min: {binding_min}
    max: {binding_max}
""")

    return config_dir

def test_minmax_within_range_succeeds():
    print("Running test_minmax_within_range_succeeds...")
    with tempfile.TemporaryDirectory() as tmp_dir:
        config_dir = write_minmax_config(tmp_dir, "ranged-prop = <2>;")
        output_dir = os.path.join(tmp_dir, "output")
        os.makedirs(output_dir)

        result = run_compiler(config_dir, output_dir)

        if result.returncode != 0:
            print(f"FAILED: Compilation should have succeeded: {result.stderr} {result.stdout}")
            return False

        print("PASSED")
        return True

def test_minmax_below_minimum_fails():
    print("Running test_minmax_below_minimum_fails...")
    with tempfile.TemporaryDirectory() as tmp_dir:
        config_dir = write_minmax_config(tmp_dir, "ranged-prop = <-1>;")
        output_dir = os.path.join(tmp_dir, "output")
        os.makedirs(output_dir)

        result = run_compiler(config_dir, output_dir)

        if result.returncode == 0:
            print("FAILED: Compilation should have failed for a below-minimum value")
            return False

        if "below minimum" not in result.stdout:
            print(f"FAILED: Expected 'below minimum' error message, got: {result.stdout}")
            return False

        print("PASSED")
        return True

def test_minmax_above_maximum_fails():
    print("Running test_minmax_above_maximum_fails...")
    with tempfile.TemporaryDirectory() as tmp_dir:
        config_dir = write_minmax_config(tmp_dir, "ranged-prop = <7>;")
        output_dir = os.path.join(tmp_dir, "output")
        os.makedirs(output_dir)

        result = run_compiler(config_dir, output_dir)

        if result.returncode == 0:
            print("FAILED: Compilation should have failed for an above-maximum value")
            return False

        if "above maximum" not in result.stdout:
            print(f"FAILED: Expected 'above maximum' error message, got: {result.stdout}")
            return False

        print("PASSED")
        return True

def test_minmax_out_of_range_default_fails():
    print("Running test_minmax_out_of_range_default_fails...")
    with tempfile.TemporaryDirectory() as tmp_dir:
        # Property omitted from the .dts entirely, so the (invalid) binding default is used.
        config_dir = write_minmax_config(tmp_dir, "", binding_default=9)
        output_dir = os.path.join(tmp_dir, "output")
        os.makedirs(output_dir)

        result = run_compiler(config_dir, output_dir)

        if result.returncode == 0:
            print("FAILED: Compilation should have failed for an out-of-range default value")
            return False

        if "above maximum" not in result.stdout:
            print(f"FAILED: Expected 'above maximum' error message, got: {result.stdout}")
            return False

        print("PASSED")
        return True

def test_minmax_symbolic_value_skips_validation():
    print("Running test_minmax_symbolic_value_skips_validation...")
    with tempfile.TemporaryDirectory() as tmp_dir:
        # A passed-through symbolic constant can't be range-checked at compile time and must
        # not be rejected just because a min/max is declared.
        config_dir = write_minmax_config(tmp_dir, "ranged-prop = <SOME_DEFINE>;")
        output_dir = os.path.join(tmp_dir, "output")
        os.makedirs(output_dir)

        result = run_compiler(config_dir, output_dir)

        if result.returncode != 0:
            print(f"FAILED: Compilation should have succeeded for a symbolic value: {result.stderr} {result.stdout}")
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
        test_compile_missing_config,
        test_minmax_within_range_succeeds,
        test_minmax_below_minimum_fails,
        test_minmax_above_maximum_fails,
        test_minmax_out_of_range_default_fails,
        test_minmax_symbolic_value_skips_validation
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
