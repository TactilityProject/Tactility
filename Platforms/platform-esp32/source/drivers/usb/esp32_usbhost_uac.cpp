// SPDX-License-Identifier: Apache-2.0
//
// USB Audio Class 1.0 output codec, as a class client beneath the shared
// esp32_usbhost controller. On attach of a headset supporting 48 kHz/16-bit/
// stereo PCM output, registers a dynamic "usb_uac0" AUDIO_CODEC_TYPE device
// (see esp32_usbhost_hid.cpp's "usb_keyboard0" for the pattern) so
// audio-stream discovers and re-binds to it; falls back to the board codec
// on disconnect. RX is intentionally unopened, to not displace the onboard mic.
#include <sdkconfig.h>
#include <soc/soc_caps.h>

#if SOC_USB_OTG_SUPPORTED && (CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S3)

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>

#include <atomic>
#include <cmath>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_heap_caps.h>
#include <usb/uac_host.h>

#define TAG "esp32_usbhost_uac"

extern "C" Driver usb_uac_codec_driver;

namespace {

constexpr uint32_t NATIVE_SAMPLE_RATE = 48000;
constexpr uint8_t NATIVE_CHANNELS = 2;
constexpr uint8_t NATIVE_BITS_PER_SAMPLE = 16;
constexpr size_t UAC_TASK_STACK = 4096;
constexpr UBaseType_t UAC_TASK_PRIORITY = 5;
constexpr size_t UAC_EVENT_QUEUE_LENGTH = 8;
// Keep the PCM ring buffer above CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL so normal
// heap allocation places it in PSRAM. Isochronous URBs remain DMA-capable.
constexpr uint32_t UAC_BUFFER_SIZE = 32768;
constexpr uint32_t UAC_BUFFER_THRESHOLD = 4000;
constexpr TickType_t UAC_TASK_POLL_INTERVAL = pdMS_TO_TICKS(200);
constexpr TickType_t UAC_STOP_TIMEOUT = pdMS_TO_TICKS(7000);
// Bound for taking UsbUacData::mutex from the codec device's stop path: the
// client task holds it across class calls (which are themselves bounded), but
// a stuck client task must not stall device-tree teardown indefinitely.
constexpr TickType_t UAC_MUTEX_STOP_TIMEOUT = pdMS_TO_TICKS(2000);

struct UacConnectEvent {
    uint8_t address;
    uint8_t interface_number;
};

struct UsbUacData {
    SemaphoreHandle_t mutex = nullptr;
    SemaphoreHandle_t task_done = nullptr;
    QueueHandle_t event_queue = nullptr;
    TaskHandle_t task = nullptr;
    uac_host_device_handle_t handle = nullptr;
    std::atomic<bool> running {false};
    std::atomic<bool> disconnect_pending {false};
    std::atomic<bool> transfer_error_pending {false};
    bool streaming = false;
    uint8_t volume = 100;
    bool muted = false;
    // The manager Device, set in manager_start_device; parent of codec_device.
    Device* manager_device = nullptr;
    // Dynamic AUDIO_CODEC_TYPE child device, constructed while a UAC output is
    // connected (mirrors esp32_usbhost_hid.cpp's kb_device).
    Device codec_device {};
    bool codec_device_active = false;
};

#define GET_DATA(device) (static_cast<UsbUacData*>(device_get_driver_data(device)))

void codec_device_construct(UsbUacData* data);
void codec_device_destruct(UsbUacData* data);

void log_heap(const char* stage) {
    LOG_I(TAG, "%s heap: internal_free=%u internal_min=%u external_free=%u",
          stage,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

bool supports_native_format(uac_host_device_handle_t handle) {
    uac_host_dev_info_t info = {};
    if (uac_host_get_device_info(handle, &info) != ESP_OK) {
        return false;
    }

    for (uint8_t alt = 1; alt <= info.iface_alt_num; ++alt) {
        uac_host_dev_alt_param_t params = {};
        if (uac_host_get_device_alt_param(handle, alt, &params) != ESP_OK
            || params.format != 1
            || params.channels != NATIVE_CHANNELS
            || params.subframe_size != sizeof(int16_t)
            || params.bit_resolution != NATIVE_BITS_PER_SAMPLE) {
            continue;
        }

        if (params.sample_freq_type == 0) {
            if (NATIVE_SAMPLE_RATE >= params.sample_freq_lower
                && NATIVE_SAMPLE_RATE <= params.sample_freq_upper) {
                return true;
            }
            continue;
        }

        for (uint8_t index = 0;
             index < params.sample_freq_type && index < UAC_FREQ_NUM_MAX;
             ++index) {
            if (params.sample_freq[index] == NATIVE_SAMPLE_RATE) {
                return true;
            }
        }
    }

    return false;
}

void queue_driver_event(uint8_t address, uint8_t interface_number,
                        uac_host_driver_event_t type, void* arg) {
    // RX intentionally unopened (see file header).
    if (type != UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        return;
    }

    auto* data = static_cast<UsbUacData*>(arg);
    const UacConnectEvent event = {
        .address = address,
        .interface_number = interface_number,
    };
    xQueueSend(data->event_queue, &event, 0);
}

void queue_device_event(uac_host_device_handle_t,
                        uac_host_device_event_t type, void* arg) {
    auto* data = static_cast<UsbUacData*>(arg);
    if (type == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        // A disconnect is a one-shot event and must not depend on queue capacity.
        data->disconnect_pending = true;
    } else if (type == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        data->transfer_error_pending = true;
    }
}

void open_output_interface(UsbUacData* data, uint8_t address, uint8_t interface_number) {
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    const bool already_open = data->handle != nullptr;
    xSemaphoreGive(data->mutex);
    if (already_open) {
        return;
    }

    const uac_host_device_config_t config = {
        .addr = address,
        .iface_num = interface_number,
        .buffer_size = UAC_BUFFER_SIZE,
        .buffer_threshold = UAC_BUFFER_THRESHOLD,
        .callback = queue_device_event,
        .callback_arg = data,
    };

    uac_host_device_handle_t handle = nullptr;
    esp_err_t result = uac_host_device_open(&config, &handle);
    if (result != ESP_OK) {
        LOG_W(TAG, "Failed to open UAC output interface: %s", esp_err_to_name(result));
        return;
    }

    if (!supports_native_format(handle)) {
        LOG_W(TAG, "Ignoring UAC output without 48 kHz, 16-bit stereo PCM");
        uac_host_device_close(handle);
        return;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (data->handle == nullptr && data->running.load()) {
        data->handle = handle;
        handle = nullptr;
    }
    xSemaphoreGive(data->mutex);

    if (handle != nullptr) {
        // Another thread opened an output in the meantime; drop ours.
        uac_host_device_close(handle);
        return;
    }

    log_heap("interface-open");
    LOG_I(TAG, "UAC output connected (48 kHz, 16-bit, stereo)");
    codec_device_construct(data);
}

bool close_output_interface(UsbUacData* data) {
    // Remove the dynamic codec device first: its device_stop() notifies
    // listeners (audio-stream re-binds output to the board codec) and waits
    // for in-flight writes, which are serialized against the handle close
    // below by data->mutex.
    codec_device_destruct(data);

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    uac_host_device_handle_t handle = data->handle;
    if (handle == nullptr) {
        xSemaphoreGive(data->mutex);
        return true;
    }
    if (data->streaming) {
        const esp_err_t stop_result = uac_host_device_stop(handle);
        if (stop_result != ESP_OK) {
            LOG_W(TAG, "Failed to stop UAC output: %s", esp_err_to_name(stop_result));
        }
        data->streaming = false;
    }

    const esp_err_t result = uac_host_device_close(handle);
    if (result == ESP_OK) {
        data->handle = nullptr;
    }
    xSemaphoreGive(data->mutex);
    if (result != ESP_OK) {
        LOG_W(TAG, "Failed to close UAC output: %s", esp_err_to_name(result));
        return false;
    }
    log_heap("interface-closed");
    LOG_I(TAG, "UAC output disconnected");
    return true;
}

void uac_client_task(void* arg) {
    auto* data = static_cast<UsbUacData*>(arg);
    UacConnectEvent event = {};

    while (data->running.load()) {
        if (data->disconnect_pending.exchange(false)) {
            if (!close_output_interface(data)) {
                data->disconnect_pending = true;
            }
        }
        if (data->transfer_error_pending.exchange(false)) {
            LOG_W(TAG, "UAC transfer error");
        }

        if (xQueueReceive(data->event_queue, &event, UAC_TASK_POLL_INTERVAL) != pdTRUE) {
            continue;
        }
        open_output_interface(data, event.address, event.interface_number);
    }

    close_output_interface(data);

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    data->task = nullptr;
    xSemaphoreGive(data->mutex);
    xSemaphoreGive(data->task_done);
    vTaskDelete(nullptr);
}

// region Dynamic codec device (usb_uac0) driver

error_t codec_start_device(Device* device) {
    // driver_data is set by the manager before device_start(); the UAC
    // interface is already open. Streaming begins lazily via the codec API's
    // open().
    return (GET_DATA(device) != nullptr) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t codec_stop_device(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    // Best-effort streaming stop; the class handle itself is closed by the
    // manager's task (close_output_interface). Bounded mutex wait so a stuck
    // client task cannot stall device-tree teardown indefinitely.
    if (xSemaphoreTake(data->mutex, UAC_MUTEX_STOP_TIMEOUT) != pdTRUE) {
        LOG_W(TAG, "codec device stop: client task busy, leaving teardown to it");
        return ERROR_NONE;
    }
    if (data->streaming && data->handle != nullptr) {
        const esp_err_t result = uac_host_device_stop(data->handle);
        if (result != ESP_OK) {
            LOG_W(TAG, "Failed to stop UAC output: %s", esp_err_to_name(result));
        }
        data->streaming = false;
    }
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

// endregion

// region AudioCodecApi

error_t open(Device* device, const AudioCodecStreamConfig* config) {
    if (config == nullptr
        || config->direction != AUDIO_CODEC_DIR_OUTPUT
        || config->sample_rate != NATIVE_SAMPLE_RATE
        || config->channels != NATIVE_CHANNELS
        || config->bits_per_sample != NATIVE_BITS_PER_SAMPLE) {
        return ERROR_NOT_SUPPORTED;
    }

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (data->handle == nullptr) {
        xSemaphoreGive(data->mutex);
        return ERROR_RESOURCE;
    }
    if (data->streaming) {
        xSemaphoreGive(data->mutex);
        return ERROR_NONE;
    }

    const uac_host_stream_config_t stream_config = {
        .channels = NATIVE_CHANNELS,
        .bit_resolution = NATIVE_BITS_PER_SAMPLE,
        .sample_freq = NATIVE_SAMPLE_RATE,
        .flags = 0,
    };
    esp_err_t result = uac_host_device_start(data->handle, &stream_config);
    if (result == ESP_OK) {
        data->streaming = true;
        result = uac_host_device_set_volume(data->handle, data->volume);
        if (result == ESP_OK) {
            result = uac_host_device_set_mute(data->handle, data->muted);
        }
        if (result != ESP_OK) {
            uac_host_device_stop(data->handle);
            data->streaming = false;
        } else {
            log_heap("stream-started");
        }
    }
    xSemaphoreGive(data->mutex);
    return esp_err_to_error(result);
}

error_t close(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (!data->streaming || data->handle == nullptr) {
        data->streaming = false;
        xSemaphoreGive(data->mutex);
        return ERROR_NONE;
    }

    const esp_err_t result = uac_host_device_stop(data->handle);
    if (result == ESP_OK) {
        data->streaming = false;
    }
    xSemaphoreGive(data->mutex);
    return esp_err_to_error(result);
}

error_t read(Device*, void*, size_t, size_t*, TickType_t) {
    return ERROR_NOT_SUPPORTED;
}

error_t write(Device* device, const void* buffer, size_t size,
              size_t* bytes_written, TickType_t timeout) {
    if (buffer == nullptr || bytes_written == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    *bytes_written = 0;

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    if (!data->streaming || data->handle == nullptr) {
        xSemaphoreGive(data->mutex);
        return ERROR_RESOURCE;
    }
    // Serializing calls that use the class handle with disconnect cleanup keeps
    // the UAC callback from closing the handle underneath a blocking write.
    const esp_err_t result = uac_host_device_write(
        data->handle,
        const_cast<uint8_t*>(static_cast<const uint8_t*>(buffer)),
        static_cast<uint32_t>(size),
        static_cast<uint32_t>(timeout));
    xSemaphoreGive(data->mutex);

    if (result != ESP_OK) {
        LOG_W(TAG, "uac_host_device_write failed: size=%u result=%s", (unsigned)size, esp_err_to_name(result));
    }

    if (result == ESP_OK) {
        *bytes_written = size;
    }
    return esp_err_to_error(result);
}

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (volume_percent < 0.0f || volume_percent > 100.0f) {
        return ERROR_OUT_OF_RANGE;
    }

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    data->volume = static_cast<uint8_t>(std::lround(volume_percent));
    const esp_err_t result = !data->streaming || data->handle == nullptr
        ? ESP_OK
        : uac_host_device_set_volume(data->handle, data->volume);
    xSemaphoreGive(data->mutex);
    return esp_err_to_error(result);
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (volume_percent == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *volume_percent = static_cast<float>(data->volume);
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    data->muted = muted;
    const esp_err_t result = !data->streaming || data->handle == nullptr
        ? ESP_OK
        : uac_host_device_set_mute(data->handle, muted);
    xSemaphoreGive(data->mutex);
    return esp_err_to_error(result);
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (muted == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    *muted = data->muted;
    xSemaphoreGive(data->mutex);
    return ERROR_NONE;
}

error_t get_native_sample_rate(Device*, AudioCodecDirection direction, uint32_t* rate_hz) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (rate_hz == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    *rate_hz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t get_native_channels(Device*, AudioCodecDirection direction, uint8_t* channels) {
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    if (channels == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    *channels = NATIVE_CHANNELS;
    return ERROR_NONE;
}

error_t get_capabilities(Device* device, AudioCodecDirection* supported_directions) {
    if (supported_directions == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(data->mutex, portMAX_DELAY);
    const bool connected = data->handle != nullptr && !data->disconnect_pending.load();
    xSemaphoreGive(data->mutex);
    if (!connected) {
        return ERROR_RESOURCE;
    }
    *supported_directions = AUDIO_CODEC_DIR_OUTPUT;
    return ERROR_NONE;
}

const AudioCodecApi codec_api = {
    .open = open,
    .close = close,
    .read = read,
    .write = write,
    .set_volume = set_volume,
    .get_volume = get_volume,
    .set_mute = set_mute,
    .get_mute = get_mute,
    .get_native_sample_rate = get_native_sample_rate,
    .get_native_channels = get_native_channels,
    .get_capabilities = get_capabilities,
    .get_input_gain_multiplier = nullptr,
};

// endregion

void codec_device_construct(UsbUacData* data) {
    if (data->codec_device_active) {
        return;
    }

    data->codec_device = Device {
        .address = 0,
        .name = "usb_uac0",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr,
    };

    if (device_construct(&data->codec_device) != ERROR_NONE) {
        LOG_E(TAG, "failed to construct USB UAC codec device");
        return;
    }
    device_set_driver_data(&data->codec_device, data);
    device_set_parent(&data->codec_device, data->manager_device);
    device_set_driver(&data->codec_device, &usb_uac_codec_driver);
    if (device_add(&data->codec_device) != ERROR_NONE) {
        LOG_E(TAG, "failed to add USB UAC codec device");
        device_destruct(&data->codec_device);
        return;
    }
    if (device_start(&data->codec_device) != ERROR_NONE) {
        LOG_E(TAG, "failed to start USB UAC codec device");
        device_remove(&data->codec_device);
        device_destruct(&data->codec_device);
        return;
    }

    data->codec_device_active = true;
}

void codec_device_destruct(UsbUacData* data) {
    if (!data->codec_device_active) {
        return;
    }
    data->codec_device_active = false;

    device_stop(&data->codec_device);
    device_remove(&data->codec_device);
    device_destruct(&data->codec_device);
}

} // namespace

extern "C" {

// Driver for the dynamic "usb_uac0" codec device. Never matched against the
// devicetree; bound directly via device_set_driver() by the manager.
Driver usb_uac_codec_driver = {
    .name = "usb_uac_codec",
    .compatible = (const char*[]) { nullptr },
    .start_device = codec_start_device,
    .stop_device = codec_stop_device,
    .api = &codec_api,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

static error_t manager_start_device(Device* device) {
    if (device_get_parent(device) == nullptr || !device_is_ready(device_get_parent(device))) {
        LOG_E(TAG, "USB host parent is not ready");
        return ERROR_RESOURCE;
    }

    auto* data = new UsbUacData();
    data->manager_device = device;
    data->mutex = xSemaphoreCreateMutex();
    data->task_done = xSemaphoreCreateBinary();
    data->event_queue = xQueueCreate(UAC_EVENT_QUEUE_LENGTH, sizeof(UacConnectEvent));
    if (data->mutex == nullptr || data->task_done == nullptr || data->event_queue == nullptr) {
        LOG_E(TAG, "Failed to allocate UAC synchronization resources");
        goto cleanup;
    }

    {
        const uac_host_driver_config_t config = {
            .create_background_task = true,
            .task_priority = UAC_TASK_PRIORITY,
            .stack_size = UAC_TASK_STACK,
            .core_id = tskNO_AFFINITY,
            .callback = queue_driver_event,
            .callback_arg = data,
        };
        const esp_err_t result = uac_host_install(&config);
        if (result != ESP_OK) {
            LOG_E(TAG, "uac_host_install failed: %s", esp_err_to_name(result));
            goto cleanup;
        }
    }

    data->running = true;
    if (xTaskCreate(uac_client_task, "usb_uac_client", UAC_TASK_STACK, data,
                    UAC_TASK_PRIORITY, &data->task) != pdPASS) {
        LOG_E(TAG, "Failed to create UAC client task");
        data->running = false;
        uac_host_uninstall();
        goto cleanup;
    }

    device_set_driver_data(device, data);
    LOG_I(TAG, "UAC manager started");
    return ERROR_NONE;

cleanup:
    if (data->event_queue != nullptr) {
        vQueueDelete(data->event_queue);
    }
    if (data->task_done != nullptr) {
        vSemaphoreDelete(data->task_done);
    }
    if (data->mutex != nullptr) {
        vSemaphoreDelete(data->mutex);
    }
    delete data;
    return ERROR_RESOURCE;
}

static error_t manager_stop_device(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    data->running = false;
    xSemaphoreTake(data->mutex, portMAX_DELAY);
    const bool task_running = data->task != nullptr;
    xSemaphoreGive(data->mutex);
    if (task_running && xSemaphoreTake(data->task_done, UAC_STOP_TIMEOUT) != pdTRUE) {
        // The task may still be inside a class-driver call. Retain every callback
        // resource (including the dynamic codec device, which lives inside
        // UsbUacData) so a late completion cannot access freed memory.
        LOG_E(TAG, "UAC client task stop timed out; resources retained");
        return ERROR_RESOURCE;
    }

    // Also removes the dynamic codec device when still active (the client task
    // normally does this via close_output_interface before exiting).
    if (!close_output_interface(data)) {
        LOG_E(TAG, "UAC output remains open; resources retained");
        return ERROR_RESOURCE;
    }

    const esp_err_t result = uac_host_uninstall();
    if (result != ESP_OK) {
        LOG_E(TAG, "uac_host_uninstall failed; resources retained: %s", esp_err_to_name(result));
        return esp_err_to_error(result);
    }

    vQueueDelete(data->event_queue);
    vSemaphoreDelete(data->task_done);
    vSemaphoreDelete(data->mutex);
    device_set_driver_data(device, nullptr);
    delete data;
    LOG_I(TAG, "UAC manager stopped");
    return ERROR_NONE;
}

Driver esp32_usbhost_uac_driver = {
    .name = "esp32_usbhost_uac",
    .compatible = (const char*[]) { "espressif,esp32-usbhost-uac", nullptr },
    .start_device = manager_start_device,
    .stop_device = manager_stop_device,
    .api = nullptr,
    .device_type = nullptr,
    .owner = nullptr,
    .internal = nullptr,
};

} // extern "C"

#endif
