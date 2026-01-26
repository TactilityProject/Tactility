def read_file(path: str):
    with open(path, "r") as file:
        result = file.read()
        return result

def write_file(path: str, content: str):
    with open(path, "w") as file:
        result = file.write(content)
        return result
