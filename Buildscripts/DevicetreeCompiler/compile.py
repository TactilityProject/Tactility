import sys

from source.printing import print_error
from source.main import main

def print_help():
    print("Usage: python compile.py [in_file] [out_path] [arguments]\n")
    print(f"\t[in_path]                 the path where the root devicetree.yaml file is")
    print(f"\t[out_path]                output folder for C file output")
    print("")
    print("Optional arguments:\n")
    print("\t--help                  prints this help text")
    print("\t--verbose               output debug info")

if __name__ == "__main__":
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print_error("Missing argument")
        print_help()
        sys.exit()
    is_verbose = "--verbose" in sys.argv
    devicetree_yaml_config = args[0]
    output_path = args[1]
    main(devicetree_yaml_config, output_path, is_verbose)

