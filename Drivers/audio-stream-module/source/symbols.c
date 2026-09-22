// SPDX-License-Identifier: Apache-2.0
#include <tactility/module.h>

#ifdef ESP_PLATFORM

#include <esp_audio_simple_dec.h>

// The software decoders live in the esp_audio_codec component and are registered once at
// module start (source/module.cpp), so apps never register decoders themselves.
const struct ModuleSymbol audio_stream_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_open),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_close),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_process),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_get_info),
    MODULE_SYMBOL_TERMINATOR
};

#else // !ESP_PLATFORM: no decoder exports outside the ESP build

const struct ModuleSymbol audio_stream_module_symbols[] = {
    MODULE_SYMBOL_TERMINATOR
};
#endif