// SPDX-License-Identifier: Apache-2.0
#include <drivers/sc2356.h>
#include <sc2356_module.h>

#include <tactility/device.h>
#include <tactility/drivers/esp32_i2c_master.h>
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

#include <driver/jpeg_encode.h>
#include <driver/ppa.h>

#include <cstring>
#include <freertos/semphr.h>

#define TAG "SC2356"

// SC2356 (SC202CS) chip ID registers (16-bit address, 8-bit value)
// PID = 0xEB52: high byte 0xEB at register 0x3107, low byte 0x52 at 0x3108
static constexpr uint8_t SC2356_REG_CHIP_ID_H[2] = { 0x31, 0x07 };
static constexpr uint8_t SC2356_CHIP_ID_H = 0xEB;
static constexpr uint8_t SC2356_REG_CHIP_ID_L[2] = { 0x31, 0x08 };
static constexpr uint8_t SC2356_CHIP_ID_L = 0x52;

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(20);
static constexpr int VIDEO_BUFFER_COUNT = 2;

static bool s_video_initialized = false;
static SemaphoreHandle_t s_video_init_mutex = nullptr;

struct Sc2356State : CameraHandleData {
    int fd;
    // Native sensor output dimensions (always 1280×720 from the sensor)
    uint32_t native_width;
    uint32_t native_height;
    // Post-rotation dimensions (swapped for 90/270)
    uint32_t width;
    uint32_t height;
    uint8_t* buffers[VIDEO_BUFFER_COUNT];
    size_t buf_lengths[VIDEO_BUFFER_COUNT];
    int last_dqbuf_index;
    jpeg_encoder_handle_t enc;
    i2c_master_bus_handle_t sccb_bus;
    CameraRotation rotation;
    ppa_client_handle_t ppa;
    // PPA output buffer — fixed size, large enough for any rotation (native_width * native_height * 2)
    uint8_t* ppa_out_buf;
    size_t ppa_out_buf_size;
    SemaphoreHandle_t rotation_mutex; // protects rotation, width, height
};

#define GET_CONFIG(device) (static_cast<const Sc2356Config*>((device)->config))

static error_t start(Device* device) {
    auto* i2c = device_get_parent(device);
    if (device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    uint8_t chip_id_h = 0, chip_id_l = 0;
    error_t err = i2c_controller_write_read(i2c, address, SC2356_REG_CHIP_ID_H, sizeof(SC2356_REG_CHIP_ID_H), &chip_id_h, 1, I2C_TIMEOUT);
    if (err != ERROR_NONE) {
        LOG_W(TAG, "WHO_AM_I read failed: %s — sensor may still be starting up", error_to_string(err));
        return ERROR_NONE;
    }

    err = i2c_controller_write_read(i2c, address, SC2356_REG_CHIP_ID_L, sizeof(SC2356_REG_CHIP_ID_L), &chip_id_l, 1, I2C_TIMEOUT);
    if (err != ERROR_NONE) {
        LOG_W(TAG, "WHO_AM_I read (low) failed: %s", error_to_string(err));
        return ERROR_NONE;
    }

    if (chip_id_h != SC2356_CHIP_ID_H || chip_id_l != SC2356_CHIP_ID_L) {
        LOG_E(TAG, "WHO_AM_I mismatch: got 0x%02X%02X, expected 0x%02X%02X", chip_id_h, chip_id_l, SC2356_CHIP_ID_H, SC2356_CHIP_ID_L);
        return ERROR_RESOURCE;
    }
    LOG_I(TAG, "WHO_AM_I OK (0x%02X%02X)", chip_id_h, chip_id_l);
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

static ppa_srm_rotation_angle_t to_ppa_rotation(CameraRotation rotation) {
    switch (rotation) {
        case CAMERA_ROTATION_90:  return PPA_SRM_ROTATION_ANGLE_90;
        case CAMERA_ROTATION_180: return PPA_SRM_ROTATION_ANGLE_180;
        case CAMERA_ROTATION_270: return PPA_SRM_ROTATION_ANGLE_270;
        default:                  return PPA_SRM_ROTATION_ANGLE_0;
    }
}

extern "C" {

error_t sc2356_open(Device* device, Sc2356Handle* out_handle) {
    if (!device || !out_handle) return ERROR_INVALID_ARGUMENT;

    auto* state = static_cast<Sc2356State*>(heap_caps_calloc(1, sizeof(Sc2356State), MALLOC_CAP_DEFAULT));
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

    // Retrieve bus handle from the parent I2C controller
    auto* i2c = device_get_parent(device);
    state->sccb_bus = esp32_i2c_master_get_bus_handle(i2c);
    if (!state->sccb_bus) {
        LOG_E(TAG, "Failed to get i2c_master bus handle from parent device");
        vSemaphoreDelete(state->rotation_mutex);
        heap_caps_free(state);
        return ERROR_RESOURCE;
    }

    // esp_video_init only needs to run once across all sc2356_open calls
    {
        SemaphoreHandle_t init_mutex = get_video_init_mutex();
        if (!init_mutex) {
            vSemaphoreDelete(state->rotation_mutex);
            heap_caps_free(state);
            return ERROR_OUT_OF_MEMORY;
        }
        xSemaphoreTake(init_mutex, portMAX_DELAY);
        if (!s_video_initialized) {
            esp_video_init_csi_config_t csi_config = {};
            csi_config.sccb_config.init_sccb  = false;
            csi_config.sccb_config.i2c_handle = state->sccb_bus;
            csi_config.sccb_config.freq       = 400000;
            csi_config.reset_pin              = static_cast<gpio_num_t>(-1);
            csi_config.pwdn_pin               = static_cast<gpio_num_t>(-1);

            esp_video_init_config_t cam_config = {};
            cam_config.csi = &csi_config;

            esp_err_t esp_err = esp_video_init(&cam_config);
            if (esp_err != ESP_OK) {
                LOG_E(TAG, "esp_video_init failed: %s", esp_err_to_name(esp_err));
                xSemaphoreGive(init_mutex);
                vSemaphoreDelete(state->rotation_mutex);
                heap_caps_free(state);
                return ERROR_RESOURCE;
            }
            s_video_initialized = true;
        }
        xSemaphoreGive(init_mutex);
    }

    // Open the CSI video device. The ISP pipeline controller (running in the background
    // after esp_video_init) applies AE/AWB/demosaicing automatically; pixel output
    // is still on /dev/video0, which delivers processed RGB565 frames.
    state->fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (state->fd < 0) {
        LOG_E(TAG, "Failed to open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        vSemaphoreDelete(state->rotation_mutex);
        heap_caps_free(state);
        return ERROR_RESOURCE;
    }

    // Query current format to get sensor dimensions
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(state->fd, VIDIOC_G_FMT, &fmt) != 0) {
        LOG_E(TAG, "VIDIOC_G_FMT failed");
        goto err_close;
    }

    // Set RGB565 output format
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(state->fd, VIDIOC_S_FMT, &fmt) != 0) {
            LOG_E(TAG, "VIDIOC_S_FMT RGB565 failed");
            goto err_close;
        }
        // Re-read to get the actual negotiated dimensions
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(state->fd, VIDIOC_G_FMT, &fmt) != 0) {
            LOG_E(TAG, "VIDIOC_G_FMT after set failed");
            goto err_close;
        }
    }

    state->native_width  = fmt.fmt.pix.width;
    state->native_height = fmt.fmt.pix.height;

    // Post-rotation dimensions (swapped for 90/270)
    if (state->rotation == CAMERA_ROTATION_90 || state->rotation == CAMERA_ROTATION_270) {
        state->width  = state->native_height;
        state->height = state->native_width;
    } else {
        state->width  = state->native_width;
        state->height = state->native_height;
    }
    LOG_I(TAG, "Video format: %lux%lu native",
          (unsigned long)state->native_width, (unsigned long)state->native_height);

    // PPA output buffer — fixed size (max of any rotation), cache-line aligned per PPA requirements
    state->ppa_out_buf_size = (size_t)state->native_width * state->native_height * 2;
    state->ppa_out_buf = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(64, state->ppa_out_buf_size, MALLOC_CAP_SPIRAM));
    if (!state->ppa_out_buf) {
        LOG_E(TAG, "Failed to alloc PPA output buffer (%zu bytes)", state->ppa_out_buf_size);
        goto err_close;
    }

    // PPA client for hardware-accelerated rotation
    {
        ppa_client_config_t ppa_cfg = {};
        ppa_cfg.oper_type = PPA_OPERATION_SRM;
        ppa_cfg.max_pending_trans_num = 1;
        if (ppa_register_client(&ppa_cfg, &state->ppa) != ESP_OK) {
            LOG_E(TAG, "ppa_register_client failed");
            goto err_close;
        }
    }

    // Request mmap buffers
    {
        struct v4l2_requestbuffers req = {};
        req.count  = VIDEO_BUFFER_COUNT;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(state->fd, VIDIOC_REQBUFS, &req) != 0) {
            LOG_E(TAG, "VIDIOC_REQBUFS failed");
            goto err_close;
        }
    }

    // Query, mmap, and queue each buffer
    for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (ioctl(state->fd, VIDIOC_QUERYBUF, &buf) != 0) {
            LOG_E(TAG, "VIDIOC_QUERYBUF[%d] failed", i);
            goto err_unmap;
        }

        state->buf_lengths[i] = buf.length;
        state->buffers[i] = static_cast<uint8_t*>(
            mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, state->fd, buf.m.offset)
        );
        if (state->buffers[i] == MAP_FAILED) {
            state->buffers[i] = nullptr;
            LOG_E(TAG, "mmap[%d] failed", i);
            goto err_unmap;
        }

        if (ioctl(state->fd, VIDIOC_QBUF, &buf) != 0) {
            LOG_E(TAG, "VIDIOC_QBUF[%d] failed", i);
            goto err_unmap;
        }
    }

    // Start streaming
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(state->fd, VIDIOC_STREAMON, &type) != 0) {
            LOG_E(TAG, "VIDIOC_STREAMON failed");
            goto err_unmap;
        }
    }

    // Create hardware JPEG encoder once; reused across all sc2356_capture_jpeg() calls
    {
        jpeg_encode_engine_cfg_t eng_cfg = { .intr_priority = 0, .timeout_ms = 1000 };
        esp_err_t je = jpeg_new_encoder_engine(&eng_cfg, &state->enc);
        if (je != ESP_OK) {
            LOG_E(TAG, "jpeg_new_encoder_engine failed: %s", esp_err_to_name(je));
            goto err_unmap;
        }
    }

    *out_handle = state;
    return ERROR_NONE;

err_unmap:
    {
        // Stop streaming before unmapping - correct V4L2 cleanup order, avoids DMA-into-
        // unmapped-buffer races. Harmless no-op if STREAMON never succeeded (e.g. it's the
        // failure that brought us here).
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(state->fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        if (state->buffers[i]) munmap(state->buffers[i], state->buf_lengths[i]);
    }
err_close:
    if (state->ppa) ppa_unregister_client(state->ppa);
    if (state->ppa_out_buf) heap_caps_free(state->ppa_out_buf);
    if (state->rotation_mutex) vSemaphoreDelete(state->rotation_mutex);
    close(state->fd);
    heap_caps_free(state);
    return ERROR_RESOURCE;
}

error_t sc2356_close(Sc2356Handle handle) {
    if (!handle) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Sc2356State*>(handle);

    if (state->enc) jpeg_del_encoder_engine(state->enc);
    if (state->ppa) ppa_unregister_client(state->ppa);

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(state->fd, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        if (state->buffers[i]) munmap(state->buffers[i], state->buf_lengths[i]);
    }

    if (state->ppa_out_buf) heap_caps_free(state->ppa_out_buf);
    if (state->rotation_mutex) vSemaphoreDelete(state->rotation_mutex);
    close(state->fd);
    // Bus handle is owned by esp32_i2c_master driver - do not delete it
    heap_caps_free(state);
    return ERROR_NONE;
}

error_t sc2356_set_rotation(Sc2356Handle handle, CameraRotation rotation) {
    if (!handle) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Sc2356State*>(handle);

    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);

    if (state->rotation == rotation) {
        xSemaphoreGive(state->rotation_mutex);
        return ERROR_NONE;
    }

    bool needs_swap = (rotation == CAMERA_ROTATION_90 || rotation == CAMERA_ROTATION_270);

    state->rotation = rotation;
    if (needs_swap) {
        state->width  = state->native_height;
        state->height = state->native_width;
    } else {
        state->width  = state->native_width;
        state->height = state->native_height;
    }

    xSemaphoreGive(state->rotation_mutex);
    return ERROR_NONE;
}

error_t sc2356_get_frame(Sc2356Handle handle, uint8_t** buf, size_t* len, uint32_t timeout_ms, uint32_t* out_width, uint32_t* out_height) {
    if (!handle || !buf || !len) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Sc2356State*>(handle);

    // Use poll to enforce timeout
    struct pollfd pfd = {};
    pfd.fd     = state->fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, static_cast<int>(timeout_ms));
    if (ret == 0) return ERROR_TIMEOUT;
    if (ret < 0) {
        LOG_E(TAG, "poll failed: %d", errno);
        return ERROR_RESOURCE;
    }

    struct v4l2_buffer vbuf = {};
    vbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(state->fd, VIDIOC_DQBUF, &vbuf) != 0) {
        LOG_E(TAG, "VIDIOC_DQBUF failed: %d", errno);
        return ERROR_RESOURCE;
    }

    state->last_dqbuf_index = static_cast<int>(vbuf.index);
    uint8_t* raw = state->buffers[vbuf.index];

    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);

    uint32_t frame_width  = state->width;
    uint32_t frame_height = state->height;

    ppa_srm_oper_config_t srm_cfg = {};
    srm_cfg.in.buffer  = raw;
    srm_cfg.in.pic_w   = srm_cfg.in.block_w = state->native_width;
    srm_cfg.in.pic_h   = srm_cfg.in.block_h = state->native_height;
    srm_cfg.in.srm_cm  = PPA_SRM_COLOR_MODE_RGB565;
    srm_cfg.out.buffer      = state->ppa_out_buf;
    srm_cfg.out.buffer_size = state->ppa_out_buf_size;
    srm_cfg.out.pic_w  = frame_width;
    srm_cfg.out.pic_h  = frame_height;
    srm_cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    srm_cfg.rotation_angle = to_ppa_rotation(state->rotation);
    srm_cfg.scale_x = srm_cfg.scale_y = 1.0f;
    srm_cfg.mode = PPA_TRANS_MODE_BLOCKING;

    esp_err_t ppa_err = ppa_do_scale_rotate_mirror(state->ppa, &srm_cfg);
    xSemaphoreGive(state->rotation_mutex);

    if (ppa_err != ESP_OK) {
        LOG_E(TAG, "ppa_do_scale_rotate_mirror failed: %s", esp_err_to_name(ppa_err));
        // Buffer was already dequeued (VIDIOC_DQBUF above) - the caller receives an error
        // and may never call sc2356_release_frame(), so return it to the queue here or
        // repeated PPA failures exhaust VIDEO_BUFFER_COUNT and poll() blocks forever.
        sc2356_release_frame(handle);
        return ERROR_RESOURCE;
    }

    *buf = state->ppa_out_buf;
    *len = (size_t)frame_width * frame_height * 2;
    if (out_width != nullptr) {
        *out_width = frame_width;
    }
    if (out_height != nullptr) {
        *out_height = frame_height;
    }
    return ERROR_NONE;
}

error_t sc2356_release_frame(Sc2356Handle handle) {
    if (!handle) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Sc2356State*>(handle);

    if (state->last_dqbuf_index < 0) return ERROR_INVALID_STATE;

    struct v4l2_buffer vbuf = {};
    vbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    vbuf.index  = static_cast<uint32_t>(state->last_dqbuf_index);
    state->last_dqbuf_index = -1;

    if (ioctl(state->fd, VIDIOC_QBUF, &vbuf) != 0) {
        LOG_E(TAG, "VIDIOC_QBUF failed: %d", errno);
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

uint32_t sc2356_get_width(Sc2356Handle handle) {
    if (!handle) return 0;
    auto* state = static_cast<Sc2356State*>(handle);
    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);
    uint32_t width = state->width;
    xSemaphoreGive(state->rotation_mutex);
    return width;
}

uint32_t sc2356_get_height(Sc2356Handle handle) {
    if (!handle) return 0;
    auto* state = static_cast<Sc2356State*>(handle);
    xSemaphoreTake(state->rotation_mutex, portMAX_DELAY);
    uint32_t height = state->height;
    xSemaphoreGive(state->rotation_mutex);
    return height;
}

error_t sc2356_capture_jpeg(Sc2356Handle handle, uint8_t** out_buf, size_t* out_len, uint8_t quality) {
    if (!handle || !out_buf || !out_len) return ERROR_INVALID_ARGUMENT;
    auto* state = static_cast<Sc2356State*>(handle);

    // Dequeue one frame. Dimensions come back atomically with the frame data itself,
    // so they can't drift if a concurrent sc2356_set_rotation() changes state->width/height.
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    uint32_t capture_width = 0;
    uint32_t capture_height = 0;
    error_t err = sc2356_get_frame(handle, &frame, &frame_len, 500, &capture_width, &capture_height);
    if (err != ERROR_NONE) return err;

    // Output buffer must be allocated with jpeg_alloc_encoder_mem for DMA alignment.
    // jpeg_alloc_encoder_mem() allocates exactly the requested size with no headroom, so the
    // hint must cover the worst case, not the typical case. At high quality settings (low
    // compression) on detailed/noisy images, a width*height (1 byte/pixel) hint can be smaller
    // than the actual encoded output, causing the DMA write to overrun the buffer - this was
    // observed as JPEG corruption confined to the tail of the image. width*height*2 matches
    // the uncompressed RGB565 source size, which JPEG output can never exceed.
    size_t out_sz = 0;
    jpeg_encode_memory_alloc_cfg_t mem_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    uint8_t* jpeg_buf = static_cast<uint8_t*>(
        jpeg_alloc_encoder_mem(capture_width * capture_height * 2, &mem_cfg, &out_sz));
    if (!jpeg_buf) {
        sc2356_release_frame(handle);
        return ERROR_OUT_OF_MEMORY;
    }

    jpeg_encode_cfg_t enc_cfg = {
        .height        = capture_height,
        .width         = capture_width,
        .src_type      = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample    = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = quality,
    };
    uint32_t encoded_size = 0;
    esp_err_t esp_err = jpeg_encoder_process(state->enc, &enc_cfg, frame,
                                             static_cast<uint32_t>(frame_len),
                                             jpeg_buf, static_cast<uint32_t>(out_sz),
                                             &encoded_size);
    sc2356_release_frame(handle);

    if (esp_err != ESP_OK || encoded_size == 0) {
        LOG_E(TAG, "jpeg_encoder_process failed: %s", esp_err_to_name(esp_err));
        heap_caps_free(jpeg_buf);
        return ERROR_RESOURCE;
    }

    *out_buf = jpeg_buf;
    *out_len = static_cast<size_t>(encoded_size);
    return ERROR_NONE;
}

CameraApi sc2356_camera_api = {
    .open = sc2356_open,
    .close = sc2356_close,
    .get_frame = sc2356_get_frame,
    .release_frame = sc2356_release_frame,
    .get_width = sc2356_get_width,
    .get_height = sc2356_get_height,
    .set_rotation = sc2356_set_rotation,
    .capture_jpeg = sc2356_capture_jpeg
};

Driver sc2356_driver = {
    .name = "sc2356",
    .compatible = (const char*[]) { "smartsens,sc2356", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &sc2356_camera_api,
    .device_type = &CAMERA_TYPE,
    .owner = &sc2356_module,
    .internal = nullptr
};

} // extern "C"
