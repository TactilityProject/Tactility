import os
from pprint import pprint

from lark import Lark

from source.files import *
from source.transformer import *
from source.generator import *
from source.binding_files import find_all_bindings
from source.binding_parser import parse_binding
from source.config import *
from source.exception import DevicetreeException

def main(config_path: str, output_path: str, verbose: bool) -> int:
    print(f"Generating devicetree code\n  config: {config_path}\n  output: {output_path}")
    if not os.path.isdir(config_path):
        raise DevicetreeException(f"Directory not found: {config_path}")

    config = parse_config(config_path, os.getcwd())
    if verbose:
        pprint(config)

    try:
        project_dir = os.path.dirname(os.path.realpath(__file__))
        grammar_path = os.path.join(project_dir, "grammar.lark")
        lark_data = read_file(grammar_path)
        dts_data = read_file(config.dts)
        lark = Lark(lark_data)
        parsed = lark.parse(dts_data)
        if verbose:
            print(parsed.pretty())
        transformed = DtsTransformer().transform(parsed)
        if verbose:
            pprint(transformed)
        binding_files = find_all_bindings(config.bindings)
        if verbose:
            print(f"Bindings found:")
            for binding_file in binding_files:
                print(f"  {binding_file}")
        if verbose:
            print(f"Parsing bindings")
        bindings = []
        for binding_file in binding_files:
            bindings.append(parse_binding(binding_file, config.bindings))
        if verbose:
            for binding in bindings:
                pprint(binding)
        generate(output_path, transformed, bindings, verbose)
        return 0
    except DevicetreeException as caught:
        print("\033[31mError: ", caught, "\033[0m")
        return 1
