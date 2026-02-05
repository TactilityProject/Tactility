from dataclasses import dataclass

@dataclass
class DtsVersion:
    version: str

@dataclass
class Device:
    node_name: str
    node_alias: str
    properties: list
    devices: list

@dataclass
class DeviceProperty:
    name: str
    type: str
    value: object

@dataclass
class PropertyValue:
    type: str
    value: object

@dataclass
class IncludeC:
    statement: str

@dataclass
class DefineC:
    statement: str

@dataclass
class BindingProperty:
    name: str
    type: str
    required: bool
    description: str

@dataclass
class Binding:
    filename: str
    compatible: list[str]
    description: str
    properties: list[BindingProperty]
    includes: list[str]
    bus: str = None
