import sys
import os

from source.printing import print_error
from source.config import parse_config

def print_help():
    print("Usage: python dependencies.py [in_file]\n")
    print(f"\t[in_file]                 the path where the root devicetree.yaml file is")

if __name__ == "__main__":
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 1:
        print_error("Missing argument")
        print_help()
        sys.exit()

    devicetree_yaml_config = args[0]
    
    if not os.path.exists(devicetree_yaml_config):
        print_error(f"Path not found: {devicetree_yaml_config}")
        sys.exit(1)

    config = parse_config(devicetree_yaml_config, os.getcwd())
    
    for dependency in config.dependencies:
        dependency_name = os.path.basename(os.path.normpath(dependency))
        print(dependency_name)
