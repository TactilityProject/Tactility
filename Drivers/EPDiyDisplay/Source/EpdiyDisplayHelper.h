#pragma once

#include "EpdiyDisplay.h"
#include <epd_board.h>
#include <epd_display.h>
#include <memory>

/**
 * Helper class to create EPDiy displays with common configurations
 */
class EpdiyDisplayHelper {
public:
    /**
     * Create a display for M5Paper S3
     * @param touch Optional touch device
     * @param temperature Display temperature in °C (default: 20)
     * @param drawMode Default draw mode (default: MODE_GL16 for non-flashing updates)
     * @param fullRefresh Use full refresh mode (default: false for partial updates)
     * @param rotation Display rotation (default: EPD_ROT_LANDSCAPE)
     */
    static std::shared_ptr<EpdiyDisplay> createM5PaperS3Display(
        std::shared_ptr<tt::hal::touch::TouchDevice> touch = nullptr,
        int temperature = 20,
        enum EpdDrawMode drawMode = MODE_GL16,
        bool fullRefresh = false,
        enum EpdRotation rotation = EPD_ROT_LANDSCAPE
    ) {
        auto config = std::make_unique<EpdiyDisplay::Configuration>(
            &epd_board_m5papers3,
            &ED047TC1,
            touch,
            static_cast<EpdInitOptions>(EPD_LUT_1K | EPD_FEED_QUEUE_32),
            static_cast<const EpdWaveform*>(EPD_BUILTIN_WAVEFORM),
            temperature,
            drawMode,
            fullRefresh,
            rotation
        );
        
        return std::make_shared<EpdiyDisplay>(std::move(config));
    }
};
