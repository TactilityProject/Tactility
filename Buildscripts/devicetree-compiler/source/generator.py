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
    if device_binding == None:
        raise Exception(f"Binding not found for {device.identifier}")
    compatible_safe = device_binding.compatible.split(",")[-1]
    return compatible_safe.replace("-", "_")

def find_device_property(device: Device, name: str) -> DeviceProperty:
    for property in device.properties:
        if property.name == name:
            return property
    return None

def find_binding_property(binding: Binding, name: str) -> BindingProperty:
    for property in binding.properties:
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

def property_to_string(property: Property) -> str:
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
    compatible_property = find_binding_property(device, "compatible")
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
    file.write(f"const static struct {type_name}_config {device_identifier}_config_instance" " = {\n")
    config_params = resolve_parameters_from_bindings(device, bindings)
    # Indent all params
    for index, config_param in enumerate(config_params):
        config_params[index] = f"\t{config_param}"
    # Join with comman and newline
    if len(config_params) > 0:
        config_params_joined = ",\n".join(config_params)
        file.write(f"{config_params_joined}\n")
    file.write("};\n\n")

def write_device(file, device: Device, parent_device: Device, bindings: list[Binding], verbose: bool):
    if verbose:
        print(f"Processing device '{device.identifier}'")
    # Assemble some pre-requisites
    type_name = get_device_type_name(device, bindings)
    compatible_property = find_binding_property(device, "compatible")
    if compatible_property is None:
        raise Exception(f"Cannot find 'compatible' property for {device.identifier}")
    identifier = get_device_identifier_safe(device)
    # Write config struct
    write_config(file, device, bindings, type_name)
    # Type & instance names
    api_type_name = f"{type_name}_api"
    api_instance_name = f"{type_name}_api_instance"
    init_function_name = f"{type_name}_init"
    deinit_function_name = f"{type_name}_deinit"
    config_instance_name = f"{identifier}_config_instance"
    # Write device struct
    file.write(f"extern const struct {api_type_name} {api_instance_name};\n\n")
    file.write("static struct device " f"{identifier}" " = {\n")
    file.write("\t.name = \"" f"{identifier}" "\",\n")
    file.write(f"\t.config = &{config_instance_name},\n")
    file.write(f"\t.api = &{api_instance_name},\n")
    file.write("\t.state = { .init_result = 0, .initialized = false },\n")
    file.write("\t.data = nullptr,\n")
    file.write("\t.operations = { ")
    file.write(f".init = {init_function_name}, ")
    file.write(f".deinit = {deinit_function_name}")
    file.write("},\n")
    file.write("\t.metadata = {\n")
    file.write("\t\t.num_node_labels = 0,\n")
    file.write("\t\t.node_labels = nullptr\n")
    file.write("\t}\n")
    file.write("};\n\n")
    for child_device in device.devices:
        write_device(file, child_device, device, bindings, verbose)

def write_device_list_entry(file, device: Device):
    compatible_property = find_binding_property(device, "compatible")
    if compatible_property is None:
        raise Exception(f"Cannot find 'compatible' property for {device.identifier}")
    identifier = get_device_identifier_safe(device)
    file.write(f"\t&{identifier},\n")
    for child in device.devices:
        write_device_list_entry(file, child)

def write_device_list(file, devices: list[Device]):
    file.write("struct device* devices_builtin[] = {\n")
    for device in devices:
        write_device_list_entry(file, device)
    # Terminator
    file.write(f"\tnullptr\n")
    file.write("};\n\n")

def generate_devicetree_c(filename: str, items: list[object], bindings: list[Binding], verbose: bool):
    with open(filename, "w") as file:
        # Write all headers first
        for item in items:
            if type(item) is IncludeC:
                write_include(file, item, verbose)
        file.write("\n")
        # Then write all devices
        devices = []
        for item in items:
            if type(item) is Device:
                devices.append(item)
                write_device(file, item, None, bindings, verbose)
        write_device_list(file, devices)
        file.write(dedent('''\
        struct device** devices_builtin_get() {
            return devices_builtin;
        }
        '''))

def generate_devicetree_h(filename: str):
    with open(filename, "w") as file:
        file.write(dedent('''\
        #pragma once
        #include <device.h>
        
        #ifdef __cplusplus
        extern "C" {
        #endif
        
        /**
         * @return an array of device* where the last item in the array is nullptr
         */
        struct device** devices_builtin_get();
        
        #ifdef __cplusplus
        }
        #endif
        '''))

def generate(output_path: str, items: list[object], bindings: list[Binding], verbose: bool):
    devicetree_c_filename = os.path.join(output_path, "devicetree.c")
    generate_devicetree_c(devicetree_c_filename, items, bindings, verbose)
    devicetree_h_filename = os.path.join(output_path, "devicetree.h")
    generate_devicetree_h(devicetree_h_filename)
