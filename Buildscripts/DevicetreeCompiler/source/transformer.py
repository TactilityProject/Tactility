from typing import List

from lark import Transformer
from lark import Token
from source.models import *
from dataclasses import dataclass

def flatten_token_array(tokens: List[Token], name: str):
    result_list = list()
    for token in tokens:
        result_list.append(token.value)
    return Token(name, result_list)

@dataclass
class NodeNameAndAlias:
    name: str
    alias: str

class DtsTransformer(Transformer):
    # Flatten the start node into a list
    def start(self, tokens):
        return tokens
    def dts_version(self, tokens: List[Token]):
        version = tokens[0].value
        if version != "dts-v1":
            raise Exception(f"Unsupported DTS version: {version}")
        return DtsVersion(version)
    def device(self, tokens: list):
        node_name = None
        node_alias = None
        properties = list()
        child_devices = list()
        for index, item in enumerate(tokens):
            if type(item) is Token and item.type == 'NODE_NAME':
                node_name = item.value
            elif type(item) is Token and item.type == 'NODE_ALIAS':
                node_alias = item.value
            elif type(item) is DeviceProperty:
                properties.append(item)
            elif type(item) is Device:
                child_devices.append(item)
        return Device(node_name, node_alias, properties, child_devices)
    def device_property(self, objects: List[object]):
        name = objects[0]
        # Boolean property has no value as the value is implied to be true
        if (len(objects) == 1) or (objects[1] is None):
            return DeviceProperty(name, "boolean", True)
        if type(objects[1]) is not PropertyValue:
            raise Exception(f"Object was not converted to PropertyValue: {objects[1]}")
        return DeviceProperty(name, objects[1].type, objects[1].value)
    def property_value(self, tokens: List):
        token = tokens[0]
        if type(token) is Token:
            raise Exception(f"Failed to convert token to PropertyValue: {token}")
        return token
    def PHANDLE(self, token: Token):
        return PropertyValue(type="phandle", value=token.value[1:])
    def values(self, object):
        return PropertyValue(type="values", value=object)
    def value(self, object):
        # PHANDLE is already converted to PropertyValue
        if isinstance(object[0], PropertyValue):
            return object[0]
        return PropertyValue(type="value", value=object[0])
    def array(self, object):
        return PropertyValue(type="array", value=object)
    def VALUE(self, token: Token):
        if token.value.startswith("&"):
            return PropertyValue(type="phandle", value=token.value[1:])
        return token.value
    def NUMBER(self, token: Token):
        return token.value
    def PROPERTY_NAME(self, token: Token):
        return token.value
    def QUOTED_TEXT(self, token: Token):
        return PropertyValue("text", token.value[1:-1])
    def quoted_text_array(self, tokens: List[Token]):
        result_list = list()
        for token in tokens:
            result_list.append(token.value)
        return PropertyValue("text_array", result_list)
    def INCLUDE_C(self, token: Token):
        return IncludeC(token.value)
    def DEFINE_C(self, token: Token):
        return DefineC(token.value)