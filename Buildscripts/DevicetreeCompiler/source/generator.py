import os.path
from textwrap import dedent
from source.models import *
from .exception import DevicetreeException

def write_include(file, include: IncludeC, verbose: bool):
    if verbose:
        print("Processing include:")
        print(f"  {include.statement}")
    file.write(include.statement)
    file.write('\n')

def write_define(file, define: DefineC, verbose: bool):
    if verbose:
        print("Processing define:")
        print(f"  {define.statement}")
    file.write(define.statement)
    file.write('\n')

def get_device_node_name_safe(device: Device):
    if device.node_name == "/":
        return "root"
    else:
        return device.node_name.replace("-", "_")

def get_device_type_name(device: Device, bindings: list[Binding]):
    device_binding = find_device_binding(device, bindings)
    if device_binding is None:
        raise DevicetreeException(f"Binding not found for {device.node_name}")
    if device_binding.compatible is None:
        raise DevicetreeException(f"Couldn't find compatible binding for {device.node_name}")
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
        raise DevicetreeException(f"property 'compatible' not found in device {device.node_name}")
    for binding in bindings:
        if binding.compatible == compatible_property.value:
            return binding
    return None

def find_binding(compatible: str, bindings: list[Binding]) -> Binding:
    for binding in bindings:
        if binding.compatible == compatible:
            return binding
    return None

def find_phandle(devices: list[Device], phandle: str):
    for device in devices:
        if device.node_name == phandle or device.node_alias == phandle:
            return f"&{get_device_node_name_safe(device)}"
    raise DevicetreeException(f"phandle '{phandle}' not found in device tree")

def property_to_string(property: DeviceProperty, devices: list[Device]) -> str:
    type = property.type
    if type == "value" or type == "int":
        return property.value
    elif type == "boolean" or type == "bool":
        return "true"
    elif type == "text":
        return f"\"{property.value}\""
    elif type == "values":
        value_list = list()
        for item in property.value:
            if isinstance(item, PropertyValue):
                value_list.append(property_to_string(DeviceProperty(name="", type=item.type, value=item.value), devices))
            else:
                value_list.append(str(item))
        return "{ " + ",".join(value_list) + " }"
    elif type == "phandle":
        return find_phandle(devices, property.value)
    else:
        raise DevicetreeException(f"property_to_string() has an unsupported type: {type}")

def resolve_parameters_from_bindings(device: Device, bindings: list[Binding], devices: list[Device]) -> list:
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise DevicetreeException(f"Cannot find 'compatible' property for {device.node_name}")
    device_binding = find_binding(compatible_property.value, bindings)
    if device_binding is None:
        raise DevicetreeException(f"Binding not found for {device.node_name} and compatible '{compatible_property.value}'")
    # Filter out system properties
    binding_properties = []
    binding_property_names = set()
    for property in device_binding.properties:
        if property.name != "compatible":
            binding_properties.append(property)
            binding_property_names.add(property.name)

    # Check for invalid properties in device
    for device_property in device.properties:
        if device_property.name in ["compatible"]:
            continue
        if device_property.name not in binding_property_names:
            raise DevicetreeException(f"Device '{device.node_name}' has invalid property '{device_property.name}'")

    # Allocate total expected configuration arguments
    result = [0] * len(binding_properties)
    for index, binding_property in enumerate(binding_properties):
        device_property = find_device_property(device, binding_property.name)
        if device_property is None:
            if binding_property.default is not None:
                temp_prop = DeviceProperty(
                    name=binding_property.name,
                    type=binding_property.type,
                    value=binding_property.default
                )
                result[index] = property_to_string(temp_prop, devices)
            elif binding_property.required:
                raise DevicetreeException(f"device {device.node_name} doesn't have property '{binding_property.name}'")
            elif binding_property.type == "bool":
                result[index] = "false"
            else:
                raise DevicetreeException(f"Device {device.node_name} doesn't have property '{binding_property.name}' and no default value is set")
        else:
            result[index] = property_to_string(device_property, devices)
    return result

def write_config(file, device: Device, bindings: list[Binding], devices: list[Device], type_name: str):
    node_name = get_device_node_name_safe(device)
    config_type = f"{type_name}_config_dt"
    config_variable_name = f"{node_name}_config"
    file.write(f"static const {config_type} {config_variable_name}" " = {\n")
    config_params = resolve_parameters_from_bindings(device, bindings, devices)
    # Indent all params
    for index, config_param in enumerate(config_params):
        config_params[index] = f"\t{config_param}"
    # Join with command and newline
    if len(config_params) > 0:
        config_params_joined = ",\n".join(config_params)
        file.write(f"{config_params_joined}\n")
    file.write("};\n\n")

def write_device_structs(file, device: Device, parent_device: Device, bindings: list[Binding], devices: list[Device], verbose: bool):
    if verbose:
        print(f"Writing device struct for '{device.node_name}'")
    # Assemble some pre-requisites
    type_name = get_device_type_name(device, bindings)
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise DevicetreeException(f"Cannot find 'compatible' property for {device.node_name}")
    node_name = get_device_node_name_safe(device)
    config_variable_name = f"{node_name}_config"
    if parent_device is not None:
        parent_node_name = get_device_node_name_safe(parent_device)
        parent_value = f"&{parent_node_name}"
    else:
        parent_value = "NULL"
    # Write config struct
    write_config(file, device, bindings, devices, type_name)
    # Write device struct
    file.write(f"static struct Device {node_name}" " = {\n")
    file.write(f"\t.name = \"{device.node_name}\",\n") # Use original name
    file.write(f"\t.config = &{config_variable_name},\n")
    file.write(f"\t.parent = {parent_value},\n")
    file.write("\t.internal = NULL\n")
    file.write("};\n\n")
    # Child devices
    for child_device in device.devices:
        write_device_structs(file, child_device, device, bindings, devices, verbose)

def write_device_init(file, device: Device, bindings: list[Binding], verbose: bool):
    if verbose:
        print(f"Processing device init code for '{device.node_name}'")
    # Assemble some pre-requisites
    compatible_property = find_device_property(device, "compatible")
    if compatible_property is None:
        raise DevicetreeException(f"Cannot find 'compatible' property for {device.node_name}")
    # Type & instance names
    node_name = get_device_node_name_safe(device)
    device_variable = node_name
    # Write device struct
    file.write("\t{ " f"&{device_variable}, \"{compatible_property.value}\"" " },\n")
    # Write children
    for child_device in device.devices:
        write_device_init(file, child_device, bindings, verbose)

# Walk the tree and gather all devices
def gather_devices(device: Device, output: list[Device]):
    output.append(device)
    for child_device in device.devices:
        gather_devices(child_device, output)

def generate_devicetree_c(filename: str, items: list[object], bindings: list[Binding], verbose: bool):
    # Create a cache for looking up device names and aliases easily
    # We still want to traverse it as a tree during code generation because of parent-setting
    devices = list()
    for item in items:
        if type(item) is Device:
            gather_devices(item, devices)

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
            elif type(item) is DefineC:
                write_define(file, item, verbose)
        file.write("\n")

        # Then write all devices
        for item in items:
            if type(item) is Device:
                write_device_structs(file, item, None, bindings, devices, verbose)
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
