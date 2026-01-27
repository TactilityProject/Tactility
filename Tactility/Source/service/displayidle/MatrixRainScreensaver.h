#pragma once

#include "Screensaver.h"
#include <array>
#include <vector>
#include <cstdint>

namespace tt::service::displayidle {

class MatrixRainScreensaver final : public Screensaver {
public:
    MatrixRainScreensaver() = default;
    MatrixRainScreensaver(const MatrixRainScreensaver&) = delete;
    MatrixRainScreensaver& operator=(const MatrixRainScreensaver&) = delete;
    MatrixRainScreensaver(MatrixRainScreensaver&&) = delete;
    MatrixRainScreensaver& operator=(MatrixRainScreensaver&&) = delete;

    static constexpr int CHAR_WIDTH = 12;
    static constexpr int CHAR_HEIGHT = 16;
    static constexpr int MIN_TRAIL_LENGTH = 8;
    static constexpr int MAX_TRAIL_LENGTH = 20;
    static constexpr int MAX_ACTIVE_DROPS = 16;
    static constexpr int MIN_TICK_DELAY = 2;   // Fastest: advance every 2 frames
    static constexpr int MAX_TICK_DELAY = 5;   // Slowest: advance every 5 frames
    static constexpr int GLOW_OFFSET = 2;

    ~MatrixRainScreensaver() override = default;

    void start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) override;
    void stop() override;
    void update(lv_coord_t screenW, lv_coord_t screenH) override;

private:
    // A single character cell in the rain
    struct RainChar {
        lv_obj_t* label = nullptr;
        lv_obj_t* glowLabel = nullptr;
        char ch = ' ';
    };

    // A raindrop - now uses integer row positions (terminal-style grid)
    struct Raindrop {
        int headRow = 0;            // Current head row position (grid-based)
        int column = 0;             // Column index
        int trailLength = 10;
        int tickDelay = 3;          // Frames between row advances
        int tickCounter = 0;        // Current tick count
        std::vector<RainChar> chars;
        bool active = false;
    };

    std::vector<Raindrop> drops_;
    std::vector<int> availableColumns_;
    lv_obj_t* overlay_ = nullptr;
    lv_coord_t screenW_ = 0;
    lv_coord_t screenH_ = 0;
    int numColumns_ = 0;
    int numRows_ = 0;
    int globalFlickerCounter_ = 0;

    // Six-color gradient palette
    static constexpr std::array<uint32_t, 6> COLOR_GRADIENT = {
        0xAAFFDD,  // 0: Bright cyan-white (head)
        0x00FF66,  // 1: Bright green
        0x00DD44,  // 2: Medium-bright green
        0x00AA33,  // 3: Medium green
        0x006622,  // 4: Dim green
        0x003311,  // 5: Dark green (tail end)
    };
    static constexpr uint32_t COLOR_GLOW = 0x002208;

    void initRaindrop(Raindrop& drop, int column);
    void resetRaindrop(Raindrop& drop);
    void updateRaindropDisplay(Raindrop& drop);
    void flickerRandomChars();
    void createRainCharLabels(RainChar& rc, int trailIndex, int trailLength);
    lv_color_t getTrailColor(int index, int trailLength) const;
    static char randomChar();
    int getRandomAvailableColumn();
    void releaseColumn(int column);
};

} // namespace tt::service::displayidle
