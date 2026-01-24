from typing import List

from lark import Transformer
from lark import Token
from source.models import *

def flatten_token_array(tokens: List[Token], name: str):
    result_list = list()
    for token in tokens:
        result_list.append(token.value)
    return Token(name, result_list)

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
        identifier = "UNKNOWN"
        properties = list()
        devices = list()
        for index, entry in enumerate(tokens):
            if index == 0:
                identifier = entry.value
            elif type(entry) is DeviceProperty:
                properties.append(entry)
            elif type(entry) is Device:
                devices.append(entry)
        return Device(identifier, properties, devices)
    def device_property(self, objects: List[object]):
        assert len(objects) == 2
        if not type(objects[1]) is PropertyValue:
            raise Exception(f"Object was not converted to PropertyValue: {objects[1]}")
        return DeviceProperty(objects[0], objects[1].type, objects[1].value)
    def property_value(self, tokens: List):
        token = tokens[0]
        if type(token) is Token:
            raise Exception(f"Failed to convert token to PropertyValue: {token}")
        return token
    def values(self, object):
        return PropertyValue(type="values", value=object)
    def value(self, object):
        return PropertyValue(type="value", value=object[0])
    def array(self, object):
        return PropertyValue(type="array", value=object)
    def VALUE(self, token: Token):
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