#pragma once

#ifdef ESP_PLATFORM

#include "Screensaver.h"
#include <array>
#include <cstdint>

namespace tt::service::displayidle {

class MystifyScreensaver final : public Screensaver {
public:
    static constexpr int NUM_POLYGONS = 2;
    static constexpr int NUM_VERTICES = 4;
    static constexpr int TRAIL_LENGTH = 8;
    static constexpr int COLOR_CHANGE_INTERVAL = 200;  // Frames between color changes (~10s at 20fps)

    ~MystifyScreensaver() override = default;
    MystifyScreensaver() = default;
    MystifyScreensaver(const MystifyScreensaver&) = delete;
    MystifyScreensaver& operator=(const MystifyScreensaver&) = delete;
    MystifyScreensaver(MystifyScreensaver&&) = delete;
    MystifyScreensaver& operator=(MystifyScreensaver&&) = delete;

    void start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) override;
    void stop() override;
    void update(lv_coord_t screenW, lv_coord_t screenH) override;

private:
    // Use floats for smooth sub-pixel movement
    struct Vertex {
        float x = 0;
        float y = 0;
        float dx = 0;
        float dy = 0;
    };

    struct Polygon {
        std::array<Vertex, NUM_VERTICES> vertices;
        // History: [trail_index][vertex_index] = {x, y}
        std::array<std::array<lv_point_precise_t, NUM_VERTICES>, TRAIL_LENGTH> history;
        // Line objects: [trail_index][edge_index]
        std::array<std::array<lv_obj_t*, NUM_VERTICES>, TRAIL_LENGTH> lines{};
        // Persistent point arrays for each line (required by lv_line_set_points)
        std::array<std::array<std::array<lv_point_precise_t, 2>, NUM_VERTICES>, TRAIL_LENGTH> linePoints;
        lv_color_t color = {};
        int historyHead = 0;
        bool historyFull = false;
        int colorChangeCounter = 0;
    };

    // Vibrant color pool for random selection
    static constexpr std::array<uint32_t, 8> COLOR_POOL = {
        0xFF00FF,  // Magenta
        0x00FFFF,  // Cyan
        0xFFFF00,  // Yellow
        0xFF6600,  // Orange
        0x00FF66,  // Spring green
        0x6600FF,  // Purple
        0xFF0066,  // Hot pink
        0x66FF00,  // Lime
    };

    std::array<Polygon, NUM_POLYGONS> polygons_;

    void initPolygon(Polygon& polygon, lv_obj_t* parent, lv_coord_t screenW, lv_coord_t screenH);
    void updateLineColors(Polygon& polygon);
};

} // namespace tt::service::displayidle

#endif // ESP_PLATFORM
