#include <Tactility/app/terminal/TerminalRendererPpa.h>

#include <graphics/pixel_buffer.h>

#include <tactility/drivers/display.h>
#include <tactility/log.h>

#ifdef ESP_PLATFORM
#include <soc/soc_caps.h>
#endif

#if defined(ESP_PLATFORM) && SOC_PPA_SUPPORTED

#include <driver/ppa.h>

constexpr auto* TAG = "TermRenderPpa";

bool TerminalRendererPpa::isSupported() {
    return true;
}

TerminalRendererPpa::~TerminalRendererPpa() {
    end();
}

bool TerminalRendererPpa::begin(Device* displayDevice) {
    panelWidth = display_get_resolution_x(displayDevice);
    panelHeight = display_get_resolution_y(displayDevice);

    // The terminal is landscape, so its buffer is the panel's dimensions transposed.
    frameWidth = panelHeight;
    frameHeight = panelWidth;

    if (!allocateCommon(displayDevice)) {
        return false;
    }

    if (monochrome) {
        // No PPA hardware here rotates packed 1bpp data - refuse rather than feed it bits it was
        // never designed for.
        LOG_E(TAG, "Monochrome displays are not supported by the PPA-rotated renderer");
        freeCommon();
        return false;
    }

    if (!acquireHwDoubleBuffer()) {
        rotatedRowBuffer = pixel_buffer_create(display_get_color_format(displayDevice), cellHeight, panelHeight);
        if (rotatedRowBuffer == nullptr) {
            LOG_E(TAG, "Failed to allocate rotation output buffer");
            end();
            return false;
        }
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

    if (!allocateFullFrameBufferIfNeeded()) {
        end();
        return false;
    }

    clearPanelOnce();

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
    if (rotatedRowBuffer != nullptr) {
        pixel_buffer_free(rotatedRowBuffer);
        rotatedRowBuffer = nullptr;
    }
    freeCommon();
}

void TerminalRendererPpa::present(int yStart, int yEnd) {
    const int rowPixelHeight = yEnd - yStart;
    pixel_buffer_msync(frameBuffer, 0, 0, frameWidth, rowPixelHeight);
    void* data = pixel_buffer_get_data(frameBuffer);

    // Rotates straight into hwFrameBuffers[backBufferIndex] below, bypassing presentRegion() - so
    // it must seed that buffer itself before this row overwrites part of it.
    if (usingHwFrameBuffer) {
        ensureBackBufferSeeded();
    }

    // A 90-degree rotation swaps axes: this source y-row becomes an output x-strip.
    // UNVERIFIED on hardware: assumes increasing source y maps to increasing output x. If rows
    // land mirrored/reversed, flip this single line to `frameHeight - yEnd` instead.
    const int rotatedX = yStart;

    // frameBuffer holds only one row, so it's described to PPA as its own self-contained picture.
    ppa_srm_oper_config_t srmConfig = {
        .in = {
            .buffer = data,
            .pic_w = static_cast<uint32_t>(frameWidth),
            .pic_h = static_cast<uint32_t>(rowPixelHeight),
            .block_w = static_cast<uint32_t>(frameWidth),
            .block_h = static_cast<uint32_t>(rowPixelHeight),
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .out = {
            .buffer = usingHwFrameBuffer ? pixel_buffer_get_data(hwFrameBuffers[backBufferIndex]) : pixel_buffer_get_data(rotatedRowBuffer),
            .buffer_size = usingHwFrameBuffer
                ? static_cast<uint32_t>(panelWidth * panelHeight * 2)
                : static_cast<uint32_t>(rowPixelHeight * panelHeight * 2),
            // hw case: sub-block of the full panel-sized buffer. fallback: rotatedRowBuffer is
            // its own small canvas, starting at (0,0).
            .pic_w = usingHwFrameBuffer ? static_cast<uint32_t>(panelWidth) : static_cast<uint32_t>(rowPixelHeight),
            .pic_h = static_cast<uint32_t>(panelHeight),
            .block_offset_x = usingHwFrameBuffer ? static_cast<uint32_t>(rotatedX) : 0u,
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

    if (usingHwFrameBuffer) {
        // PPA already wrote straight into hwFrameBuffers[backBufferIndex] at the right offset.
        hwBufferDirty = true;
    } else {
        presentRegion(rotatedRowBuffer, rotatedX, 0, rowPixelHeight, panelHeight);
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

void TerminalRendererPpa::present(int, int) {
}

#endif
