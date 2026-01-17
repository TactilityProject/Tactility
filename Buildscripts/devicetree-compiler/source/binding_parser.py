import yaml
import os
from .models import Binding, BindingProperty

def parse_binding(file_path: str, binding_dirs: list[str]) -> Binding:
    with open(file_path, 'r') as f:
        data = yaml.safe_load(f)

    description = data.get('description', '')
    bus = data.get('bus', None)
    properties_dict = {}

    # Handle inclusions
    includes = data.get('include', [])
    for include_file in includes:
        include_path = None
        for binding_dir in binding_dirs:
            potential_path = os.path.join(binding_dir, include_file)
            if os.path.exists(potential_path):
                include_path = potential_path
                break

        if not include_path:
            print(f"Warning: Could not find include file {include_file}")

        parent_binding = parse_binding(include_path, binding_dirs)
        if not description and parent_binding.description:
            description = parent_binding.description
        if not bus and parent_binding.bus:
            bus = parent_binding.bus
        for prop in parent_binding.properties:
            properties_dict[prop.name] = prop
        for include in parent_binding.includes:
            includes.append(include)

    # Parse local properties
    properties_raw = data.get('properties', {})
    for name, details in properties_raw.items():
        prop = BindingProperty(
            name=name,
            type=details.get('type', 'unknown'),
            required=details.get('required', False),
            description=details.get('description', '').strip(),
        )
        properties_dict[name] = prop

    filename = os.path.basename(file_path)
    compatible = filename.removesuffix(".yaml").removesuffix(".yml")
    return Binding(
        filename=filename,
        compatible=compatible,
        description=description.strip(),
        properties=list(properties_dict.values()),
        includes=includes,
        bus=bus
    )
