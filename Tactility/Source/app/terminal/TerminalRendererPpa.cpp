#include <Tactility/app/terminal/TerminalRendererPpa.h>

#include <tactility/drivers/display.h>
#include <tactility/log.h>

#ifdef ESP_PLATFORM
#include <soc/soc_caps.h>
#endif

#if defined(ESP_PLATFORM) && SOC_PPA_SUPPORTED

#include <driver/ppa.h>

#include <esp_cache.h>
#include <esp_heap_caps.h>

#include <cstring>

constexpr auto* TAG = "TermRenderPpa";

bool TerminalRendererPpa::isSupported() {
    return true;
}

TerminalRendererPpa::~TerminalRendererPpa() {
    end();
}

bool TerminalRendererPpa::begin(Device* displayDevice) {
    // These report the panel's native orientation, which on Tab5 is portrait.
    panelWidth = display_get_resolution_x(displayDevice);
    panelHeight = display_get_resolution_y(displayDevice);

    // The terminal is landscape, so its buffer is the panel's dimensions transposed.
    frameWidth = panelHeight;
    frameHeight = panelWidth;

    if (!allocateCommon(displayDevice)) {
        return false;
    }

    // Output: either the panel's own double buffers, or one PSRAM buffer we push manually.
    if (!acquireHwDoubleBuffer()) {
        const size_t frameBytes = static_cast<size_t>(frameWidth) * frameHeight * sizeof(uint16_t);
        rotatedBuffer = static_cast<uint16_t*>(heap_caps_aligned_alloc(64, frameBytes, MALLOC_CAP_SPIRAM));
        if (rotatedBuffer == nullptr) {
            LOG_E(TAG, "Failed to allocate rotation output buffer");
            end();
            return false;
        }
        memset(rotatedBuffer, 0, frameBytes);
    }

    ppa_client_config_t ppaConfig = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
        .flags = { .allow_pd = 0 },
    };
    ppa_client_handle_t client = nullptr;
    if (ppa_register_client(&ppaConfig, &client) != ESP_OK) {
        LOG_E(TAG, "Failed to register PPA client");
        end();
        return false;
    }
    ppaClient = client;

    LOG_I(TAG, "Terminal %dx%d cells (%dx%d px), %dx%d landscape on %dx%d panel",
             cols, rowCount, cellWidth, cellHeight,
             frameWidth, frameHeight, panelWidth, panelHeight);
    return true;
}

void TerminalRendererPpa::end() {
    if (ppaClient != nullptr) {
        ppa_unregister_client(static_cast<ppa_client_handle_t>(ppaClient));
        ppaClient = nullptr;
    }
    if (rotatedBuffer != nullptr) {
        heap_caps_free(rotatedBuffer);
        rotatedBuffer = nullptr;
    }
    freeCommon();
}

void TerminalRendererPpa::present() {
    const size_t frameBytes = static_cast<size_t>(frameWidth) * frameHeight * sizeof(uint16_t);
    esp_cache_msync(frameBuffer, frameBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    uint16_t* out = usingHwFrameBuffer ? hwFrameBuffers[backBufferIndex] : rotatedBuffer;

    // Rotate the landscape frame 90 degrees into the panel's portrait orientation. No scaling:
    // the buffer was sized as the panel's transpose, so it maps 1:1.
    ppa_srm_oper_config_t srmConfig = {
        .in = {
            .buffer = frameBuffer,
            .pic_w = static_cast<uint32_t>(frameWidth),
            .pic_h = static_cast<uint32_t>(frameHeight),
            .block_w = static_cast<uint32_t>(frameWidth),
            .block_h = static_cast<uint32_t>(frameHeight),
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .out = {
            .buffer = out,
            .buffer_size = static_cast<uint32_t>(panelWidth * panelHeight * 2),
            .pic_w = static_cast<uint32_t>(panelWidth),
            .pic_h = static_cast<uint32_t>(panelHeight),
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .alpha_fix_val = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
        .user_data = nullptr,
    };

    if (ppa_do_scale_rotate_mirror(static_cast<ppa_client_handle_t>(ppaClient), &srmConfig) != ESP_OK) {
        LOG_W(TAG, "PPA rotation failed");
        return;
    }

    display_draw_bitmap(display, 0, 0, panelWidth, panelHeight, out);

    if (usingHwFrameBuffer) {
        backBufferIndex = 1 - backBufferIndex;
    }
}

#else // !(ESP_PLATFORM && SOC_PPA_SUPPORTED)

bool TerminalRendererPpa::isSupported() {
    return false;
}

TerminalRendererPpa::~TerminalRendererPpa() = default;

bool TerminalRendererPpa::begin(Device*) {
    return false;
}

void TerminalRendererPpa::end() {
}

void TerminalRendererPpa::present() {
}

#endif
