from dataclasses import dataclass, field
import yaml
import os

from .exception import DevicetreeException

@dataclass
class DeviceTreeConfig:
    dependencies: list[str] = field(default_factory=list)
    bindings: list[str] = field(default_factory=list)
    dts: str = ""
    dts_only: bool = False

def parse_config(file_path: str, project_root: str) -> DeviceTreeConfig:
    """
    Parses devicetree.yaml and recursively finds dependencies.
    Returns a list of DeviceTreeConfig objects in post-order (dependencies first).
    """
    config = DeviceTreeConfig([], [], "")
    visited = set()

    def _parse_recursive(current_path: str, is_root: bool):
        abs_path = os.path.abspath(current_path)
        if abs_path in visited:
            return
        visited.add(abs_path)

        # Try to see if it's a directory and contains devicetree.yaml
        if os.path.isdir(abs_path):
            abs_path = os.path.join(abs_path, "devicetree.yaml")

        with open(abs_path, 'r') as f:
            data = yaml.safe_load(f) or {}

        # Handle dependencies before adding current config (post-order)
        deps = data.get("dependencies", [])
        for dep in deps:
            # Dependencies are relative to project_root
            dep_path = os.path.join(project_root, dep)
            _parse_recursive(dep_path, False)

        if is_root:
            config.dependencies += deps
            dts_path = data.get("dts", "")
            config.dts = os.path.join(current_path, dts_path)
            dts_only = data.get("dts-only", False)
            if not isinstance(dts_only, bool):
                raise DevicetreeException(f"'dts-only' must be a boolean, got {dts_only!r}")
            config.dts_only = dts_only

        bindings = data.get("bindings", "")
        if bindings:
            bindings_resolved = os.path.join(current_path, bindings)
            config.bindings.append(bindings_resolved)

    _parse_recursive(file_path, True)
    return config
