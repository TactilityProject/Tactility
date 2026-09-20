// SPDX-License-Identifier: Apache-2.0
#include <audio_decoder/module.h>

#ifdef ESP_PLATFORM

#include <simple_dec/esp_audio_simple_dec.h>
#include <esp_audio_dec_default.h>

#include <esp_log.h>

// Registers the Kconfig-selected default audio decoders (trimmed to MP3+PCM, see
// Buildscripts/sdkconfig/default.properties) once at module start, so
// esp_audio_simple_dec_open() is ready for any app to use without each one remembering to
// register decoders itself - registration is idempotent-per-boot and cheap, but the actual
// decoder object code only needs to exist once in the firmware, not duplicated per app.
static error_t audio_decoder_module_start(void) {
    esp_audio_err_t dec_register_result = esp_audio_dec_register_default();
    if (dec_register_result != ESP_AUDIO_ERR_OK) {
        // Not a boot failure: apps can still use everything else. But without this, a later
        // esp_audio_simple_dec_open() failure has no explanation in the log.
        ESP_LOGE("audio-decoder", "esp_audio_dec_register_default failed: %d", (int) dec_register_result);
    }
    return ERROR_NONE;
}

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    DEFINE_MODULE_SYMBOL(esp_audio_simple_check_audio_type),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_open),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_process),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_get_info),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_reset),
    DEFINE_MODULE_SYMBOL(esp_audio_simple_dec_close),
    MODULE_SYMBOL_TERMINATOR
};

Module audio_decoder_module = {
    .name = "audio-decoder",
    .start = audio_decoder_module_start,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}

#else // !ESP_PLATFORM

extern "C" {

Module audio_decoder_module = {
    .name = "audio-decoder",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr,
};

}

#endif // ESP_PLATFORM
