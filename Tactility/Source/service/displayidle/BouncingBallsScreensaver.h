#pragma once
#ifdef ESP_PLATFORM

#include "Screensaver.h"
#include <array>
#include <cstdint>

namespace tt::service::displayidle {

class BouncingBallsScreensaver final : public Screensaver {
public:
    BouncingBallsScreensaver() = default;
    ~BouncingBallsScreensaver() override = default;
    BouncingBallsScreensaver(const BouncingBallsScreensaver&) = delete;
    BouncingBallsScreensaver& operator=(const BouncingBallsScreensaver&) = delete;
    BouncingBallsScreensaver(BouncingBallsScreensaver&&) = delete;
    BouncingBallsScreensaver& operator=(BouncingBallsScreensaver&&) = delete;

    void start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) override;
    void stop() override;
    void update(lv_coord_t screenW, lv_coord_t screenH) override;

private:
    static constexpr int BALL_SIZE = 20;
    static constexpr int NUM_BALLS = 5;
    static constexpr int COLOR_FADE_STEPS = 15;

    struct Ball {
        lv_obj_t* obj = nullptr;
        lv_coord_t x = 0;
        lv_coord_t y = 0;
        lv_coord_t dx = 0;
        lv_coord_t dy = 0;
        uint8_t currentR = 0, currentG = 0, currentB = 0;
        uint8_t targetR = 0, targetG = 0, targetB = 0;
        int colorStep = 0;
    };

    static constexpr std::array<uint32_t, 8> COLORS = {
        0xFF0000,  // Red
        0x00FF00,  // Green
        0x0000FF,  // Blue
        0xFFFF00,  // Yellow
        0xFF00FF,  // Magenta
        0x00FFFF,  // Cyan
        0xFF8000,  // Orange
        0x80FF00,  // Lime
    };

    std::array<Ball, NUM_BALLS> balls_;

    void initBall(Ball& ball, lv_obj_t* parent, lv_coord_t screenW, lv_coord_t screenH, int index);
    static void setRandomTargetColor(Ball& ball);
    static void updateBallColor(Ball& ball);
};

} // namespace tt::service::displayidle

#endif // ESP_PLATFORM
