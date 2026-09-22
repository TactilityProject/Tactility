// SPDX-License-Identifier: Apache-2.0
#include <cjson_symbols/module.h>

#include <cJSON.h>

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    // cJSON.h - every real exported function (not the header's inline SetIntValue/
    // SetNumberValue/SetBoolValue/ArrayForEach macros, which expand at the app's own compile
    // time and need no export).
    DEFINE_MODULE_SYMBOL(cJSON_Version),
    // cJSON_InitHooks intentionally not exported: it overrides cJSON's process-global
    // malloc/free hooks, which every app and the firmware itself share - one app could break
    // allocation for everyone.
    DEFINE_MODULE_SYMBOL(cJSON_Parse),
    DEFINE_MODULE_SYMBOL(cJSON_ParseWithLength),
    DEFINE_MODULE_SYMBOL(cJSON_ParseWithOpts),
    DEFINE_MODULE_SYMBOL(cJSON_ParseWithLengthOpts),
    DEFINE_MODULE_SYMBOL(cJSON_Print),
    DEFINE_MODULE_SYMBOL(cJSON_PrintUnformatted),
    DEFINE_MODULE_SYMBOL(cJSON_PrintBuffered),
    DEFINE_MODULE_SYMBOL(cJSON_PrintPreallocated),
    DEFINE_MODULE_SYMBOL(cJSON_Delete),
    DEFINE_MODULE_SYMBOL(cJSON_GetArraySize),
    DEFINE_MODULE_SYMBOL(cJSON_GetArrayItem),
    DEFINE_MODULE_SYMBOL(cJSON_GetObjectItem),
    DEFINE_MODULE_SYMBOL(cJSON_GetObjectItemCaseSensitive),
    DEFINE_MODULE_SYMBOL(cJSON_HasObjectItem),
    DEFINE_MODULE_SYMBOL(cJSON_GetErrorPtr),
    DEFINE_MODULE_SYMBOL(cJSON_GetStringValue),
    DEFINE_MODULE_SYMBOL(cJSON_GetNumberValue),
    DEFINE_MODULE_SYMBOL(cJSON_IsInvalid),
    DEFINE_MODULE_SYMBOL(cJSON_IsFalse),
    DEFINE_MODULE_SYMBOL(cJSON_IsTrue),
    DEFINE_MODULE_SYMBOL(cJSON_IsBool),
    DEFINE_MODULE_SYMBOL(cJSON_IsNull),
    DEFINE_MODULE_SYMBOL(cJSON_IsNumber),
    DEFINE_MODULE_SYMBOL(cJSON_IsString),
    DEFINE_MODULE_SYMBOL(cJSON_IsArray),
    DEFINE_MODULE_SYMBOL(cJSON_IsObject),
    DEFINE_MODULE_SYMBOL(cJSON_IsRaw),
    DEFINE_MODULE_SYMBOL(cJSON_CreateNull),
    DEFINE_MODULE_SYMBOL(cJSON_CreateTrue),
    DEFINE_MODULE_SYMBOL(cJSON_CreateFalse),
    DEFINE_MODULE_SYMBOL(cJSON_CreateBool),
    DEFINE_MODULE_SYMBOL(cJSON_CreateNumber),
    DEFINE_MODULE_SYMBOL(cJSON_CreateString),
    DEFINE_MODULE_SYMBOL(cJSON_CreateRaw),
    DEFINE_MODULE_SYMBOL(cJSON_CreateArray),
    DEFINE_MODULE_SYMBOL(cJSON_CreateObject),
    DEFINE_MODULE_SYMBOL(cJSON_CreateStringReference),
    DEFINE_MODULE_SYMBOL(cJSON_CreateObjectReference),
    DEFINE_MODULE_SYMBOL(cJSON_CreateArrayReference),
    DEFINE_MODULE_SYMBOL(cJSON_CreateIntArray),
    DEFINE_MODULE_SYMBOL(cJSON_CreateFloatArray),
    DEFINE_MODULE_SYMBOL(cJSON_CreateDoubleArray),
    DEFINE_MODULE_SYMBOL(cJSON_CreateStringArray),
    DEFINE_MODULE_SYMBOL(cJSON_AddItemToArray),
    DEFINE_MODULE_SYMBOL(cJSON_AddItemToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddItemToObjectCS),
    DEFINE_MODULE_SYMBOL(cJSON_AddItemReferenceToArray),
    DEFINE_MODULE_SYMBOL(cJSON_AddItemReferenceToObject),
    DEFINE_MODULE_SYMBOL(cJSON_DetachItemViaPointer),
    DEFINE_MODULE_SYMBOL(cJSON_DetachItemFromArray),
    DEFINE_MODULE_SYMBOL(cJSON_DeleteItemFromArray),
    DEFINE_MODULE_SYMBOL(cJSON_DetachItemFromObject),
    DEFINE_MODULE_SYMBOL(cJSON_DetachItemFromObjectCaseSensitive),
    DEFINE_MODULE_SYMBOL(cJSON_DeleteItemFromObject),
    DEFINE_MODULE_SYMBOL(cJSON_DeleteItemFromObjectCaseSensitive),
    DEFINE_MODULE_SYMBOL(cJSON_InsertItemInArray),
    DEFINE_MODULE_SYMBOL(cJSON_ReplaceItemViaPointer),
    DEFINE_MODULE_SYMBOL(cJSON_ReplaceItemInArray),
    DEFINE_MODULE_SYMBOL(cJSON_ReplaceItemInObject),
    DEFINE_MODULE_SYMBOL(cJSON_ReplaceItemInObjectCaseSensitive),
    DEFINE_MODULE_SYMBOL(cJSON_Duplicate),
    DEFINE_MODULE_SYMBOL(cJSON_Compare),
    DEFINE_MODULE_SYMBOL(cJSON_Minify),
    DEFINE_MODULE_SYMBOL(cJSON_AddNullToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddTrueToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddFalseToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddBoolToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddNumberToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddStringToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddRawToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddObjectToObject),
    DEFINE_MODULE_SYMBOL(cJSON_AddArrayToObject),
    DEFINE_MODULE_SYMBOL(cJSON_SetNumberHelper),
    DEFINE_MODULE_SYMBOL(cJSON_SetValuestring),
    DEFINE_MODULE_SYMBOL(cJSON_malloc),
    DEFINE_MODULE_SYMBOL(cJSON_free),
    MODULE_SYMBOL_TERMINATOR
};

Module cjson_module = {
    .name = "cjson",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
