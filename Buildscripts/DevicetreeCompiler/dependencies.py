import sys
import os

from source.printing import print_error
from source.config import parse_config

def print_help():
    print("Usage: python dependencies.py [path]\n")
    print("\t[in_file]                 the path where the root devicetree.yaml file is")

if __name__ == "__main__":
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 1:
        print_error("Missing argument")
        print_help()
        sys.exit(1)

    yaml_directory = args[0]
    
    if not os.path.exists(yaml_directory):
        print_error(f"Path not found: {yaml_directory}")
        sys.exit(1)

    config = parse_config(yaml_directory, os.getcwd())

    # Device module is added first because it's started first:
    # It creates the root device, so it must exist before its children.
    device_dependency = os.path.basename(os.path.normpath(yaml_directory))
    print(device_dependency)
    for dependency in config.dependencies:
        dependency_name = os.path.basename(os.path.normpath(dependency))
        print(dependency_name)
