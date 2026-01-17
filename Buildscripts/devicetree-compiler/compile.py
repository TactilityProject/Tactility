import os

from source.printing import *
from source.main import *

def print_help():
    print("Usage: python compile.py [in_file] [out_path] [arguments]\n")
    print(f"\t[in_file]                 the .dts file")
    print(f"\t[out_path]                output folder for C file output")
    print("")
    print("Optional arguments:\n")
    print("\t--help                  prints this help text")
    print("\t--verbose               output debug info")

if __name__ == "__main__":
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    if len(sys.argv) < 3:
        print_error("Missing argument")
        print_help()
        sys.exit()
    is_verbose = "--verbose" in sys.argv
    devictree_yaml_config = sys.argv[1]
    output_path = sys.argv[2]
    main(devictree_yaml_config, output_path, is_verbose)

