import os

def find_bindings(directory_path: str) -> list[str]:
    yaml_files = []
    for root, dirs, files in os.walk(directory_path):
        for file in files:
            if file.endswith(".yaml"):
                full_path = os.path.join(root, file)
                yaml_files.append(os.path.abspath(full_path))
    return yaml_files

def find_all_bindings(directory_paths: list[str]) -> list[str]:
    yaml_files = []
    for directory_path in directory_paths:
        new_paths = find_bindings(directory_path)
        yaml_files += new_paths
    return yaml_files