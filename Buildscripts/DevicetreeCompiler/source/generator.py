import os.path
from textwrap import dedent

from source.models import *

def write_include(file, include: IncludeC, verbose: bool):
    if verbose:
        print("Processing include:")
        print(f"  {include.statement}")
    file.write(include.statement)
    file.write('\n')

def get_device_identifier_safe(device: Device):
    if device.identifier == "/":
        return "root"
    else:
        return device.identifier

def get_device_type_name(device: Device, bindings: list[Binding]):
    device_binding = find_device_binding(device, bindings)
    if device_binding is None:
        raise Exception(f"Binding not found for {device.identifier}")
    if device_binding.compatible is None:
        raise Exception(f"Couldn't find compatible binding for {device.identifier}")
    compatible_safe = device_binding.compatible.split(",")[-1]
    return compatible_safe.replace("-", "_")

def find_device_property(device: Device, name: str) -> DeviceProperty:
    for property in device.properties:
        if property.name == name:
            return property
    return None

def find_device_binding(device: Device, bindings: list[Binding]) -> Binding:
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise Exception(f"property 'compatible' not found in device {device.identifier}")
    for binding in bindings:
        if binding.compatible == compatible_property.value:
            return binding
    return None

def find_binding(compatible: str, bindings: list[Binding]) -> Binding:
    for binding in bindings:
        if binding.compatible == compatible:
            return binding
    return None

def property_to_string(property: DeviceProperty) -> str:
    type = property.type
    if type == "value":
        return property.value
    elif type == "text":
        return f"\"{property.value}\""
    elif type == "values":
        return "{ " + ",".join(property.value) + " }"
    else:
        raise Exception(f"property_to_string() has an unsupported type: {type}")

def resolve_parameters_from_bindings(device: Device, bindings: list[Binding]) -> list:
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise Exception(f"Cannot find 'compatible' property for {device.identifier}")
    device_binding = find_binding(compatible_property.value, bindings)
    if device_binding is None:
        raise Exception(f"Binding not found for {device.identifier} and compatible '{compatible_property.value}'")
    # Filter out system properties
    binding_properties = []
    for property in device_binding.properties:
        if property.name != "compatible":
            binding_properties.append(property)
    # Allocate total expected configuration arguments
    result = [0] * len(binding_properties)
    for index, binding_property in enumerate(binding_properties):
        device_property = find_device_property(device, binding_property.name)
        if device_property is None:
            if binding_property.required:
                raise Exception(f"device {device.identifier} doesn't have property '{binding_property.name}'")
            else:
                result[index] = '0'
        else:
            result[index] = property_to_string(device_property)
    return result

def write_config(file, device: Device, bindings: list[Binding], type_name: str):
    device_identifier = get_device_identifier_safe(device)
    config_type = f"{type_name}_config_dt"
    config_variable_name = f"{device_identifier}_config"
    file.write(f"static const {config_type} {config_variable_name}" " = {\n")
    config_params = resolve_parameters_from_bindings(device, bindings)
    # Indent all params
    for index, config_param in enumerate(config_params):
        config_params[index] = f"\t{config_param}"
    # Join with command and newline
    if len(config_params) > 0:
        config_params_joined = ",\n".join(config_params)
        file.write(f"{config_params_joined}\n")
    file.write("};\n\n")

def write_device_structs(file, device: Device, parent_device: Device, bindings: list[Binding], verbose: bool):
    if verbose:
        print(f"Writing device struct for '{device.identifier}'")
    # Assemble some pre-requisites
    type_name = get_device_type_name(device, bindings)
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise Exception(f"Cannot find 'compatible' property for {device.identifier}")
    identifier = get_device_identifier_safe(device)
    config_variable_name = f"{identifier}_config"
    if parent_device is not None:
        parent_identifier = get_device_identifier_safe(parent_device)
        parent_value = f"&{parent_identifier}"
    else:
        parent_value = "NULL"
    # Write config struct
    write_config(file, device, bindings, type_name)
    # Write device struct
    file.write(f"static struct Device {identifier}" " = {\n")
    file.write(f"\t.name = \"{device.identifier}\",\n") # Use original name
    file.write(f"\t.config = &{config_variable_name},\n")
    file.write(f"\t.parent = {parent_value},\n")
    file.write("};\n\n")
    # Child devices
    for child_device in device.devices:
        write_device_structs(file, child_device, device, bindings, verbose)

def write_device_init(file, device: Device, bindings: list[Binding], verbose: bool):
    if verbose:
        print(f"Processing device init code for '{device.identifier}'")
    # Assemble some pre-requisites
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise Exception(f"Cannot find 'compatible' property for {device.identifier}")
    # Type & instance names
    identifier = get_device_identifier_safe(device)
    device_variable = identifier
    # Write device struct
    file.write("\t{ " f"&{device_variable}, \"{compatible_property.value}\"" " },\n")
    # Write children
    for child_device in device.devices:
        write_device_init(file, child_device, bindings, verbose)

def generate_devicetree_c(filename: str, items: list[object], bindings: list[Binding], verbose: bool):
    with open(filename, "w") as file:
        file.write(dedent('''\
        // Default headers
        #include <tactility/device.h>
        // DTS headers
        '''))

        # Write all headers first
        for item in items:
            if type(item) is IncludeC:
                write_include(file, item, verbose)
        file.write("\n")

        # Then write all devices
        for item in items:
            if type(item) is Device:
                write_device_structs(file, item, None, bindings, verbose)
        # Init function body start
        file.write("struct CompatibleDevice devicetree_devices[] = {\n")
        # Init function body logic
        for item in items:
            if type(item) is Device:
                write_device_init(file, item, bindings, verbose)
        # Init function body end
        file.write("\t{ NULL, NULL },\n")
        file.write("};\n")

def generate_devicetree_h(filename: str):
    with open(filename, "w") as file:
        file.write(dedent('''\
        #pragma once
        #include <tactility/error.h>
        
        #ifdef __cplusplus
        extern "C" {
        #endif
        
        extern struct CompatibleDevice devicetree_devices[];
        
        #ifdef __cplusplus
        }
        #endif
        '''))

def generate(output_path: str, items: list[object], bindings: list[Binding], verbose: bool):
    if not os.path.exists(output_path):
        os.makedirs(output_path)
    devicetree_c_filename = os.path.join(output_path, "devicetree.c")
    generate_devicetree_c(devicetree_c_filename, items, bindings, verbose)
    devicetree_h_filename = os.path.join(output_path, "devicetree.h")
    generate_devicetree_h(devicetree_h_filename)
