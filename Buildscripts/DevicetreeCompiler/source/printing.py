import sys

if sys.platform == "win32":
    SHELL_COLOR_RED = ""
    SHELL_COLOR_ORANGE = ""
    SHELL_COLOR_RESET = ""
else:
    SHELL_COLOR_RED = "\033[91m"
    SHELL_COLOR_ORANGE = "\033[93m"
    SHELL_COLOR_RESET = "\033[m"

def print_warning(message):
    print(f"{SHELL_COLOR_ORANGE}WARNING: {message}{SHELL_COLOR_RESET}")

def print_error(message):
    print(f"{SHELL_COLOR_RED}ERROR: {message}{SHELL_COLOR_RESET}")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)
