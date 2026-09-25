// SPDX-License-Identifier: Apache-2.0
#include <drivers/gc0308.h>
#include <gc0308_module.h>

#include <tactility/device.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <esp_heap_caps.h>
#include <esp_video_device.h>
#include <esp_video_init.h>
#include <freertos/task.h>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>

#include <esp_jpeg_enc.h>

#include <cstring>
#include <freertos/semphr.h>

#define TAG "GC0308"

static constexpr int VIDEO_BUFFER_COUNT = 2;
static bool s_video_initialized = false;
static SemaphoreHandle_t s_video_init_mutex = nullptr;
static uint32_t s_video_handle_count = 0;

struct Gc0308State : CameraHandleData {
    int fd;
    // Native sensor output dimensions
    uint32_t native_width;
    uint32_t native_height;
    // Frames are returned in the requested orientation.
    uint32_t width;
    uint32_t height;
    uint8_t* buffers[VIDEO_BUFFER_COUNT];
    size_t buf_lengths[VIDEO_BUFFER_COUNT];
    int last_dqbuf_index;
    uint8_t* frame_buf;
    size_t frame_buf_size;
    jpeg_enc_handle_t encoder;
    i2c_master_bus_handle_t sccb_bus;
    CameraRotation rotation;
    SemaphoreHandle_t rotation_mutex; // protects rotation, width, height
    uint32_t encoder_width;
    uint32_t encoder_height;
};

#define GET_CONFIG(device) (static_cast<const Gc0308Config*>((device)->config))

static gpio_num_t pin_or_nc(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

static CameraRotation add_rotation(CameraRotation rotation, uint16_t offset) {
    return static_cast<CameraRotation>((static_cast<uint16_t>(rotation) + offset) % 360);
}

static error_t reset_pulse(GpioDescriptor* descriptor) {
    // Release is attempted unconditionally, even if assert failed - leaving the expander output
    // asserted on an assert failure would hold the sensor in reset for the rest of boot.
    const bool assert_ok = gpio_descriptor_set_level(descriptor, true) == ERROR_NONE; // assert
    vTaskDelay(pdMS_TO_TICKS(10));
    const bool release_ok = gpio_descriptor_set_level(descriptor, false) == ERROR_NONE; // release
    vTaskDelay(pdMS_TO_TICKS(10));
    return (assert_ok && release_ok) ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t start(Device* device) {
    auto* i2c = device_get_parent(device);
    if (device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    const auto* config = GET_CONFIG(device);
    //auto address = config->address;

    // Reset is pulsed (not just held released) so the sensor reaches a known state regardless of
    // whatever it inherited from a previous boot
    if (config->pin_reset.gpio_controller != nullptr) {
        // Merge with the devicetree-supplied flags (e.g. GPIO_FLAG_ACTIVE_LOW) rather than
        // overwriting them, so a board that wires reset active-high isn't silently forced to
        // active-low polarity.
        auto* reset_descriptor = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, config->pin_reset.flags | GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
        if (reset_descriptor == nullptr) {
            LOG_E(TAG, "Failed to acquire reset pin");
            return ERROR_RESOURCE;
        }
        error_t reset_error = reset_pulse(reset_descriptor);
        gpio_descriptor_release(reset_descriptor);
        if (reset_error != ERROR_NONE) {
            LOG_E(TAG, "Failed to pulse reset pin");
            return reset_error;
        }
    }

    return ERROR_NONE;
}

static error_t stop([[maybe_unused]] Device* device) {
    return ERROR_NONE;
}

static SemaphoreHandle_t get_video_init_mutex() {
    static portMUX_TYPE creation_lock = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&creation_lock);
    if (!s_video_init_mutex) {
        s_video_init_mutex = xSemaphoreCreateMutex();
        if (!s_video_init_mutex) {
            LOG_E(TAG, "Failed to create video init mutex");
        }
    }
    portEXIT_CRITICAL(&creation_lock);
    return s_video_init_mutex;
}

extern "C" {

error_t gc0308_open(Device* device, Gc0308Handle* out_handle) {
    if (!device || !out_handle) return ERROR_INVALID_ARGUMENT;

    auto* state = static_cast<Gc0308State*>(heap_caps_calloc(1, sizeof(Gc0308State), MALLOC_CAP_DEFAULT));
    if (!state) return ERROR_OUT_OF_MEMORY;
    state->device = device;
    state->fd = -1;
    state->last_dqbuf_index = -1;
    state->rotation_mutex = xSemaphoreCreateMutex();
    if (!state->rotation_mutex) {
        heap_caps_free(state);
        return ERROR_OUT_OF_MEMORY;
    }

    state->rotation = CAMERA_ROTATION_0;
    struct v4l2_format fmt = {};

    // Retrieve bus handle from the parent I2C controller
    auto* i2c = device_get_parent(device);
    state->sccb_bus = esp32_i2c_master_get_bus_handle(i2c);
    if (!state->sccb_bus) {
        LOG_E(TAG, "Failed to get i2c_master bus handle from parent device");
        vSemaphoreDelete(state->rotation_mutex);
        heap_caps_free(state);
        return ERROR_RESOURCE;
    }

    const auto* config = GET_CONFIG(device);
    bool video_handle_acquired = false;

    // esp_video_init only needs to run once across all gc0308_open calls.
    {
        SemaphoreHandle_t init_mutex = get_video_init_mutex();
        if (!init_mutex) {
            vSemaphoreDelete(state->rotation_mutex);
            heap_caps_free(state);
            return ERROR_OUT_OF_MEMORY;
        }
        xSemaphoreTake(init_mutex, portMAX_DELAY);
        if (!s_video_initialized) {
            esp_video_init_dvp_config_t dvp_config = {
                .sccb_config = {
                    .init_sccb = false,
                    .i2c_handle = state->sccb_bus,
                    .freq = 100000,
                },
                .reset_pin = GPIO_NUM_NC,
                .pwdn_pin = GPIO_NUM_NC,
                .dvp_pin = {
                    .data_width = CAM_CTLR_DATA_WIDTH_8,
                    .data_io = {
                        static_cast<gpio_num_t>(config->pin_d0.pin),
                        static_cast<gpio_num_t>(config->pin_d1.pin),
                        static_cast<gpio_num_t>(config->pin_d2.pin),
                        static_cast<gpio_num_t>(config->pin_d3.pin),
                        static_cast<gpio_num_t>(config->pin_d4.pin),
                        static_cast<gpio_num_t>(config->pin_d5.pin),
                        static_cast<gpio_num_t>(config->pin_d6.pin),
                        static_cast<gpio_num_t>(config->pin_d7.pin),
                    },
                    .vsync_io = static_cast<gpio_num_t>(config->pin_vsync.pin),
                    .de_io = static_cast<gpio_num_t>(config->pin_de.pin),
                    .pclk_io = static_cast<gpio_num_t>(config->pin_pclk.pin),
                    .xclk_io = pin_or_nc(config->pin_xclk),
                },
                .xclk_freq = config->xclk_frequency_hz,
            };
            esp_video_init_config_t video_config = { .dvp = &dvp_config };
            esp_err_t esp_err = esp_video_init_with_flags(&video_config, ESP_VIDEO_INIT_FLAGS_DVP);
            if (esp_err != ESP_OK) {
                LOG_E(TAG, "esp_video_init DVP failed: %s", esp_err_to_name(esp_err));
                xSemaphoreGive(init_mutex);
                vSemaphoreDelete(state->rotation_mutex);
                heap_caps_free(state);
                return ERROR_RESOURCE;
            }
            s_video_initialized = true;
        }
        ++s_video_handle_count;
        video_handle_acquired = true;
        xSemaphoreGive(init_mutex);
    }

    state->fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDONLY);
    if (state->fd < 0) {
        LOG_E(TAG, "Failed to open %s", ESP_VIDEO_DVP_DEVICE_NAME);
        goto err_close;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(state->fd, VIDIOC_G_FMT, &fmt) != 0) {
        LOG_E(TAG, "VIDIOC_G_FMT failed");
        goto err_close;
    }
    state->native_width = fmt.fmt.pix.width;
    state->native_height = fmt.fmt.pix.height;
    state->frame_buf_size = (size_t)state->native_width * state->native_height * 2;
    state->rotation = add_rotation(CAMERA_ROTATION_0, config->rotation_offset);
    if (state->rotation == CAMERA_ROTATION_90 || state->rotation == CAMERA_ROTATION_270) {
        state->width = state->native_height;
        state->height = state->native_width;
    } else {
        state->width = state->native_width;
        state->height = state->native_height;
    }
    state->frame_buf = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, state->frame_buf_size, MALLOC_CAP_SPIRAM));
    if (!state->frame_buf) goto err_close;

    {
        struct v4l2_requestbuffers request = {};
        request.count = VIDEO_BUFFER_COUNT;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        if (ioctl(state->fd, VIDIOC_REQBUFS, &request) != 0) goto err_close;
    }
    for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        struct v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (ioctl(state->fd, VIDIOC_QUERYBUF, &buffer) != 0) goto err_unmap;
        state->buf_lengths[i] = buffer.length;
        state->buffers[i] = static_cast<uint8_t*>(mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, state->fd, buffer.m.offset));
        if (state->buffers[i] == MAP_FAILED) {
            state->buffers[i] = nullptr;
            goto err_unmap;
        }
        if (ioctl(state->fd, VIDIOC_QBUF, &buffer) != 0) goto err_unmap;
    }
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(state->fd, VIDIOC_STREAMON, &type) != 0) goto err_unmap;
    }

    {
        jpeg_enc_config_t encoder_config = {
            .width = static_cast<int>(state->width),
            .height = static_cast<int>(state->height),
            .src_type = JPEG_PIXEL_FORMAT_RGB565_LE,
            .subsampling = JPEG_SUBSAMPLE_420,
            .quality = 80,
            .rotate = JPEG_ROTATE_0D,
            .task_enable = false,
            .hfm_task_priority = 13,
            .hfm_task_core = 1,
        };
        if (jpeg_enc_open(&encoder_config, &state->encoder) != JPEG_ERR_OK) goto err_unmap;
        state->encoder_width = state->width;
        state->encoder_height = state->height;
    }

    LOG_I(TAG, "Video format: %lux%lu native", (unsigned long)state->native_width, (unsigned long)state->native_height);

    *out_handle = state;
    return ERROR_NONE;

err_unmap:
    if (state->encoder) {
        jpeg_enc_close(state->encoder);
        state->encoder = nullptr;
    }
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(state->fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        if (state->buffers[i]) munmap(state->buffers[i], state->buf_lengths[i]);
    }
err_close:
    if (state->fd >= 0) close(state->fd);
    if (video_handle_acquired) {
        SemaphoreHandle_t init_mutex = get_video_init_mutex();
        xSemaphoreTake(init_mutex, portMAX_DELAY);
        --s_video_handle_count;
        if (s_video_handle_count == 0 && s_video_initialized) {
            esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP);
            s_video_initialized = false;
        }
        xSemaphoreGive(init_mutex);
    }
    vSemaphoreDelete(state->rotation_mutex);
    heap_caps_free(state->frame_buf);
    heap_caps_free(state);
    return ERROR_RESOURCE;
}

error_t gc0308_close(Gc0308Handle handle) {
    if (!handle) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Gc0308State*>(handle);

    if (state->encoder) jpeg_enc_close(state->encoder);
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(state->fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        if (state->buffers[i]) munmap(state->buffers[i], state->buf_lengths[i]);
    }
    heap_caps_free(state->frame_buf);
    close(state->fd);
    SemaphoreHandle_t init_mutex = get_video_init_mutex();
    xSemaphoreTake(init_mutex, portMAX_DELAY);
    if (s_video_handle_count > 0) --s_video_handle_count;
    if (s_video_handle_count == 0 && s_video_initialized) {
        esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP);
        s_video_initialized = false;
    }
    xSemaphoreGive(init_mutex);
    if (state->rotation_mutex) vSemaphoreDelete(state->rotation_mutex);
    // Bus handle is owned by esp32_i2c_master driver - do not delete it
    heap_caps_free(state);
    return ERROR_NONE;
}

error_t gc0308_set_rotation(Gc0308Handle handle, CameraRotation rotation) {
    if (!handle) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Gc0308State*>(handle);
    CameraRotation effective_rotation = add_rotation(rotation, GET_CONFIG(state->device)->rotation_offset);

    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);

    if (state->rotation == effective_rotation) {
        xSemaphoreGive(state->rotation_mutex);
        return ERROR_NONE;
    }

    bool needs_swap = (effective_rotation == CAMERA_ROTATION_90 || effective_rotation == CAMERA_ROTATION_270);

    state->rotation = effective_rotation;
    if (needs_swap) {
        state->width = state->native_height;
        state->height = state->native_width;
    } else {
        state->width = state->native_width;
        state->height = state->native_height;
    }

    xSemaphoreGive(state->rotation_mutex);
    return ERROR_NONE;
}

error_t gc0308_get_frame(Gc0308Handle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height) {
    if (!handle || !buf || !len) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Gc0308State*>(handle);

    struct pollfd poll_descriptor = { .fd = state->fd, .events = POLLIN };
    int poll_result = poll(&poll_descriptor, 1, static_cast<int>(timeout_ms));
    if (poll_result == 0) return ERROR_TIMEOUT;
    if (poll_result < 0) return ERROR_RESOURCE;

    struct v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(state->fd, VIDIOC_DQBUF, &buffer) != 0) return ERROR_RESOURCE;
    state->last_dqbuf_index = static_cast<int>(buffer.index);

    if (buffer.index >= VIDEO_BUFFER_COUNT || buffer.bytesused != state->frame_buf_size ||
        (buffer.flags & V4L2_BUF_FLAG_ERROR) != 0) {
        gc0308_release_frame(handle);
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);
    CameraRotation rotation = state->rotation;
    uint32_t frame_width = state->width;
    uint32_t frame_height = state->height;

    size_t copy_length = state->frame_buf_size;
    const uint8_t* source = state->buffers[buffer.index];
    for (uint32_t y = 0; y < frame_height; y++) {
        for (uint32_t x = 0; x < frame_width; x++) {
            uint32_t source_x;
            uint32_t source_y;
            switch (rotation) {
                case CAMERA_ROTATION_90:
                    source_x = y;
                    source_y = state->native_height - 1 - x;
                    break;
                case CAMERA_ROTATION_180:
                    source_x = state->native_width - 1 - x;
                    source_y = state->native_height - 1 - y;
                    break;
                case CAMERA_ROTATION_270:
                    source_x = state->native_width - 1 - y;
                    source_y = x;
                    break;
                default:
                    source_x = x;
                    source_y = y;
                    break;
            }

            size_t source_offset = ((size_t)source_y * state->native_width + source_x) * 2;
            size_t destination_offset = ((size_t)y * frame_width + x) * 2;
            if (source_offset + 1 >= copy_length) continue;
            state->frame_buf[destination_offset] = source[source_offset + 1];
            state->frame_buf[destination_offset + 1] = source[source_offset];
        }
    }
    xSemaphoreGive(state->rotation_mutex);

    *buf = state->frame_buf;
    *len = (size_t)frame_width * frame_height * 2;
    if (out_width != nullptr) {
        *out_width = frame_width;
    }
    if (out_height != nullptr) {
        *out_height = frame_height;
    }
    return ERROR_NONE;
}

error_t gc0308_release_frame(Gc0308Handle handle) {
    if (!handle) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Gc0308State*>(handle);

    if (state->last_dqbuf_index < 0) return ERROR_INVALID_STATE;
    struct v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = static_cast<uint32_t>(state->last_dqbuf_index);
    state->last_dqbuf_index = -1;
    return ioctl(state->fd, VIDIOC_QBUF, &buffer) == 0 ? ERROR_NONE : ERROR_RESOURCE;
}

uint32_t gc0308_get_width(Gc0308Handle handle) {
    if (!handle) return 0;
    auto* state = static_cast<Gc0308State*>(handle);
    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);
    uint32_t width = state->width;
    xSemaphoreGive(state->rotation_mutex);
    return width;
}

uint32_t gc0308_get_height(Gc0308Handle handle) {
    if (!handle) return 0;
    auto* state = static_cast<Gc0308State*>(handle);
    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);
    uint32_t height = state->height;
    xSemaphoreGive(state->rotation_mutex);
    return height;
}

error_t gc0308_capture_jpeg(Gc0308Handle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality) {
    if (!handle || !out_buf || !out_len) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Gc0308State*>(handle);

    // Dequeue one frame. Dimensions come back atomically with the frame data itself,
    // so they can't drift if a concurrent gc0308_set_rotation() changes state->width/height.
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    error_t err = gc0308_get_frame(handle, &frame, &frame_len, 500, &frame_width, &frame_height);
    if (err != ERROR_NONE) return err;
    if (frame == nullptr || frame_len == 0) {
        gc0308_release_frame(handle);
        return ERROR_RESOURCE;
    }

    size_t image_size = (size_t)frame_width * frame_height * 2;
    uint8_t* aligned_input = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, image_size, MALLOC_CAP_SPIRAM));
    uint8_t* jpeg_output = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, image_size, MALLOC_CAP_SPIRAM));
    if (!aligned_input || !jpeg_output) {
        heap_caps_free(aligned_input);
        heap_caps_free(jpeg_output);
        gc0308_release_frame(handle);
        return ERROR_OUT_OF_MEMORY;
    }
    memcpy(aligned_input, frame, frame_len);
    if (state->encoder_width != frame_width || state->encoder_height != frame_height) {
        if (state->encoder) jpeg_enc_close(state->encoder);
        state->encoder = nullptr;
        jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
        cfg.width = static_cast<int>(frame_width);
        cfg.height = static_cast<int>(frame_height);
        cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
        cfg.subsampling = JPEG_SUBSAMPLE_420;
        cfg.quality = quality;
        if (jpeg_enc_open(&cfg, &state->encoder) != JPEG_ERR_OK) {
            heap_caps_free(aligned_input);
            heap_caps_free(jpeg_output);
            gc0308_release_frame(handle);
            return ERROR_RESOURCE;
        }
        state->encoder_width = frame_width;
        state->encoder_height = frame_height;
    }
    jpeg_enc_set_quality(state->encoder, quality);
    int encoded_size = 0;
    jpeg_error_t encode_error = jpeg_enc_process(state->encoder, aligned_input, static_cast<int>(frame_len), jpeg_output, static_cast<int>(image_size), &encoded_size);
    heap_caps_free(aligned_input);
    gc0308_release_frame(handle);

    if (encode_error != JPEG_ERR_OK || encoded_size <= 0) {
        LOG_E(TAG, "JPEG conversion failed: %d", static_cast<int>(encode_error));
        heap_caps_free(jpeg_output);
        return ERROR_RESOURCE;
    }

    *out_buf = jpeg_output;
    *out_len = encoded_size;
    return ERROR_NONE;
}

CameraApi gc0308_camera_api = {
    .open = gc0308_open,
    .close = gc0308_close,
    .get_frame = gc0308_get_frame,
    .release_frame = gc0308_release_frame,
    .get_width = gc0308_get_width,
    .get_height = gc0308_get_height,
    .set_rotation = gc0308_set_rotation,
    .capture_jpeg = gc0308_capture_jpeg
};

Driver gc0308_driver = {
    .name = "gc0308",
    .compatible = (const char*[]) { "galaxycore,gc0308", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &gc0308_camera_api,
    .device_type = &CAMERA_TYPE,
    .owner = &gc0308_module,
    .internal = nullptr
};

} // extern "C"
