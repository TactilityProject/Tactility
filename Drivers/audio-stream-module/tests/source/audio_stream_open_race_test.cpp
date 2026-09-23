// Regression tests for the audio-stream open/hotplug race in open_stream():
// while a codec is being opened (slot reserved), a codec device event can re-bind
// the direction to another codec. The abort path must release the reservation --
// before the fix it checked `*slot == handle` (never true at that point, the handle
// is only committed afterwards), stranding the reservation sentinel in the slot so
// every later open of that direction failed with ERROR_INVALID_STATE.
//
// The race is reproduced deterministically: a mock codec's open() blocks on a gate
// semaphore, holding open_stream() mid-window, while the test starts a second codec
// device (its DEVICE_EVENT_STARTED re-binds the direction through the module's own
// device listener).

#include "doctest.h"

#include <atomic>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/concurrent/thread.h>

extern "C" {
// Exported by the audio-stream module (see source/audio_stream.cpp bottom).
extern Driver audio_stream_driver;
extern Device audio_stream_device;
}

// region Mock codec

struct MockCodecState {
    AudioCodecDirection capabilities = AUDIO_CODEC_DIR_OUTPUT;
    bool fail_open = false;
    // When set, open() blocks until the gate is given, letting the test hold
    // open_stream() in its mid-open window (slot reserved, codec not yet opened).
    SemaphoreHandle_t open_gate = nullptr;
    std::atomic<int> open_count{0};
    int close_count = 0;
};

static MockCodecState state_a;
static MockCodecState state_b;

#define MOCK_STATE(device) (static_cast<MockCodecState*>(const_cast<void*>((device)->config)))

static error_t mock_start(Device* device) {
    return ERROR_NONE;
}

static error_t mock_stop(Device* device) {
    return ERROR_NONE;
}

static error_t mock_open(Device* device, const AudioCodecStreamConfig* config) {
    auto* state = MOCK_STATE(device);
    state->open_count++;
    if (state->open_gate != nullptr) {
        xSemaphoreTake(state->open_gate, portMAX_DELAY);
    }
    return state->fail_open ? ERROR_RESOURCE : ERROR_NONE;
}

static error_t mock_close(Device* device) {
    MOCK_STATE(device)->close_count++;
    return ERROR_NONE;
}

static error_t mock_read(Device* device, void* data, size_t data_size, size_t* bytes_read, TickType_t timeout) {
    return ERROR_NOT_SUPPORTED;
}

static error_t mock_write(Device* device, const void* data, size_t data_size, size_t* bytes_written, TickType_t timeout) {
    return ERROR_NOT_SUPPORTED;
}

static error_t mock_set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    return ERROR_NONE;
}

static error_t mock_get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    *volume_percent = 100.0f;
    return ERROR_NONE;
}

static error_t mock_set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    return ERROR_NONE;
}

static error_t mock_get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    *muted = false;
    return ERROR_NONE;
}

static error_t mock_get_native_sample_rate(Device* device, AudioCodecDirection direction, uint32_t* rate_hz) {
    *rate_hz = 48000;
    return ERROR_NONE;
}

static error_t mock_get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    *channels = 2;
    return ERROR_NONE;
}

static error_t mock_get_capabilities(Device* device, AudioCodecDirection* supported_directions) {
    *supported_directions = MOCK_STATE(device)->capabilities;
    return ERROR_NONE;
}

static error_t mock_get_input_gain_multiplier(Device* device, float* gain) {
    *gain = 1.0f;
    return ERROR_NONE;
}

static const AudioCodecApi MOCK_API = {
    .open = mock_open,
    .close = mock_close,
    .read = mock_read,
    .write = mock_write,
    .set_volume = mock_set_volume,
    .get_volume = mock_get_volume,
    .set_mute = mock_set_mute,
    .get_mute = mock_get_mute,
    .get_native_sample_rate = mock_get_native_sample_rate,
    .get_native_channels = mock_get_native_channels,
    .get_capabilities = mock_get_capabilities,
    .get_input_gain_multiplier = mock_get_input_gain_multiplier,
};

static Driver mock_codec_driver = {
    .name = "audio_stream_test_codec",
    .compatible = (const char*[]) { "audio-stream-test-codec", nullptr },
    .start_device = mock_start,
    .stop_device = mock_stop,
    .api = &MOCK_API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

static Device codec_a = {
    .address = 0,
    .name = "test_codec_a",
    .config = &state_a,
    .parent = nullptr,
    .internal = nullptr,
};

static Device codec_b = {
    .address = 0,
    .name = "test_codec_b",
    .config = &state_b,
    .parent = nullptr,
    .internal = nullptr,
};

// endregion

static void reset_mock_state(MockCodecState& state, AudioCodecDirection capabilities) {
    if (state.open_gate != nullptr) {
        vSemaphoreDelete(state.open_gate);
        state.open_gate = nullptr;
    }
    state.capabilities = capabilities;
    state.fail_open = false;
    state.open_count = 0;
    state.close_count = 0;
}

struct OpenRequest {
    Device* stream;
    AudioStreamConfig config;
    AudioStreamHandle handle = nullptr;
    error_t result = ERROR_UNDEFINED;
};

static int32_t open_output_thread(void* context) {
    auto* request = static_cast<OpenRequest*>(context);
    request->result = audio_stream_open_output(request->stream, &request->config, &request->handle);
    return 0;
}

static void teardown_devices_and_drivers() {
    // Only tear down devices this test case actually started (each case starts a
    // different subset; device_stop on a never-constructed device faults).
    if (device_is_added(&codec_a)) {
        device_stop(&codec_a);
        device_remove(&codec_a);
        device_destruct(&codec_a);
    }
    if (device_is_added(&codec_b)) {
        device_stop(&codec_b);
        device_remove(&codec_b);
        device_destruct(&codec_b);
    }
    if (device_is_added(&audio_stream_device)) {
        device_stop(&audio_stream_device);
        device_remove(&audio_stream_device);
        device_destruct(&audio_stream_device);
    }
    driver_remove_destruct(&mock_codec_driver);
    driver_remove_destruct(&audio_stream_driver);
}

TEST_CASE("open_stream reservations are released on codec re-bind and failed codec open") {
    // Codec A is both-capable (binds output and input); codec B is output-only, so
    // starting it re-binds the output direction away from A.
    reset_mock_state(state_a, AUDIO_CODEC_DIR_BOTH);
    reset_mock_state(state_b, AUDIO_CODEC_DIR_OUTPUT);

    REQUIRE_EQ(driver_construct_add(&audio_stream_driver), ERROR_NONE);
    REQUIRE_EQ(driver_construct_add(&mock_codec_driver), ERROR_NONE);
    REQUIRE_EQ(device_construct_add_start(&audio_stream_device, "audio-stream"), ERROR_NONE);
    REQUIRE_EQ(device_construct_add_start(&codec_a, "audio-stream-test-codec"), ERROR_NONE);

    // Hold open_stream() mid-open: the slot is reserved and the codec open blocks.
    state_a.open_gate = xSemaphoreCreateBinary();
    REQUIRE(state_a.open_gate != nullptr);

    OpenRequest request = {
        .stream = &audio_stream_device,
        .config = { .sample_rate = 44100, .bits_per_sample = 16, .channels = 2 },
    };
    Thread* thread = thread_alloc_full("open_race", 4096, open_output_thread, &request, -1);
    REQUIRE(thread != nullptr);
    REQUIRE_EQ(thread_start(thread), ERROR_NONE);

    // Wait until the open is inside the codec (reservation is set by then).
    for (int i = 0; i < 500 && state_a.open_count == 0; ++i) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    REQUIRE_EQ(state_a.open_count, 1);

    // Re-bind the output direction mid-open: codec B's DEVICE_EVENT_STARTED makes it
    // the preferred output codec. The module's listener leaves the reserved slot alone.
    REQUIRE_EQ(device_construct_add_start(&codec_b, "audio-stream-test-codec"), ERROR_NONE);

    // Release the gate; the open aborts because its codec no longer matches the slot.
    xSemaphoreGive(state_a.open_gate);
    REQUIRE_EQ(thread_join(thread, pdMS_TO_TICKS(10000), pdMS_TO_TICKS(5)), ERROR_NONE);
    thread_free(thread);

    REQUIRE_EQ(request.result, ERROR_RESOURCE);
    REQUIRE_EQ(request.handle, nullptr);
    REQUIRE_EQ(state_a.close_count, 1); // the aborted open undid its codec open

    // The regression: the reservation must be released, so the retry open succeeds
    // (before the fix this failed permanently with ERROR_INVALID_STATE).
    AudioStreamConfig config = { .sample_rate = 44100, .bits_per_sample = 16, .channels = 2 };
    AudioStreamHandle handle = nullptr;
    REQUIRE_EQ(audio_stream_open_output(&audio_stream_device, &config, &handle), ERROR_NONE);
    REQUIRE(handle != nullptr);
    REQUIRE_EQ(state_b.open_count, 1);

    // Normal close still succeeds, and another open/close cycle works afterwards.
    REQUIRE_EQ(audio_stream_close(handle), ERROR_NONE);
    REQUIRE_EQ(state_b.close_count, 1);
    handle = nullptr;
    REQUIRE_EQ(audio_stream_open_output(&audio_stream_device, &config, &handle), ERROR_NONE);
    REQUIRE_EQ(audio_stream_close(handle), ERROR_NONE);

    // A failed codec open must also release the reservation: the retry reaches the
    // codec again (a stranded reservation would return ERROR_INVALID_STATE instead),
    // and once the codec succeeds the stream opens and closes normally.
    state_b.fail_open = true;
    REQUIRE_EQ(audio_stream_open_output(&audio_stream_device, &config, &handle), ERROR_RESOURCE);
    REQUIRE_EQ(audio_stream_open_output(&audio_stream_device, &config, &handle), ERROR_RESOURCE);
    REQUIRE_EQ(state_b.open_count, 4); // 1 + 1 + 2 failed attempts
    state_b.fail_open = false;
    REQUIRE_EQ(audio_stream_open_output(&audio_stream_device, &config, &handle), ERROR_NONE);
    REQUIRE_EQ(audio_stream_close(handle), ERROR_NONE);

    teardown_devices_and_drivers();
}
