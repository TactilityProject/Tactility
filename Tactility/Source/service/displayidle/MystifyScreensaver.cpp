#include "MystifyScreensaver.h"
#include <cmath>
#include <cstdlib>

namespace tt::service::displayidle {

// Helper to generate random float in range [min, max]
static float randomFloat(float min, float max) {
    if (max <= min) return min;
    return min + (max - min) * static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

void MystifyScreensaver::start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) {
    for (auto& polygon : polygons_) {
        initPolygon(polygon, overlay, screenW, screenH);
    }
}

void MystifyScreensaver::stop() {
    for (auto& polygon : polygons_) {
        for (auto& trailLines : polygon.lines) {
            for (auto& line : trailLines) {
                line = nullptr;  // Objects deleted by parent overlay
            }
        }
        polygon.historyHead = 0;
        polygon.historyFull = false;
    }
}

void MystifyScreensaver::update(lv_coord_t screenW, lv_coord_t screenH) {
    constexpr float minSpeed = 0.5f;
    constexpr float maxSpeed = 2.5f;

    for (auto& polygon : polygons_) {
        // Periodic color change
        polygon.colorChangeCounter++;
        if (polygon.colorChangeCounter >= COLOR_CHANGE_INTERVAL) {
            polygon.colorChangeCounter = 0;
            uint32_t colorHex = COLOR_POOL[rand() % COLOR_POOL.size()];
            polygon.color = lv_color_hex(colorHex);
            updateLineColors(polygon);
        }

        // Move vertices
        for (auto& vertex : polygon.vertices) {
            vertex.x += vertex.dx;
            vertex.y += vertex.dy;

            // Bounce off edges with slight angle variation for more organic movement
            if (vertex.x <= 0) {
                vertex.x = 0;
                vertex.dx = std::fabs(vertex.dx) + randomFloat(-0.2f, 0.2f);
            } else if (vertex.x >= screenW - 1) {
                vertex.x = static_cast<float>(screenW - 1);
                vertex.dx = -std::fabs(vertex.dx) + randomFloat(-0.2f, 0.2f);
            }

            if (vertex.y <= 0) {
                vertex.y = 0;
                vertex.dy = std::fabs(vertex.dy) + randomFloat(-0.2f, 0.2f);
            } else if (vertex.y >= screenH - 1) {
                vertex.y = static_cast<float>(screenH - 1);
                vertex.dy = -std::fabs(vertex.dy) + randomFloat(-0.2f, 0.2f);
            }

            // Clamp speeds to prevent runaway acceleration or stalling
            if (std::fabs(vertex.dx) < minSpeed) vertex.dx = (vertex.dx >= 0 ? minSpeed : -minSpeed);
            if (std::fabs(vertex.dy) < minSpeed) vertex.dy = (vertex.dy >= 0 ? minSpeed : -minSpeed);
            if (std::fabs(vertex.dx) > maxSpeed) vertex.dx = (vertex.dx >= 0 ? maxSpeed : -maxSpeed);
            if (std::fabs(vertex.dy) > maxSpeed) vertex.dy = (vertex.dy >= 0 ? maxSpeed : -maxSpeed);
        }

        // Shift history
        int newHead = polygon.historyHead + 1;
        if (newHead >= TRAIL_LENGTH) {
            newHead = 0;
            polygon.historyFull = true;
        }
        polygon.historyHead = newHead;

        // Store current positions at head
        for (int v = 0; v < NUM_VERTICES; v++) {
            polygon.history[newHead][v].x = polygon.vertices[v].x;
            polygon.history[newHead][v].y = polygon.vertices[v].y;
        }

        // Pre-calculate history indices
        int histIndices[TRAIL_LENGTH];
        for (int t = 0; t < TRAIL_LENGTH; t++) {
            int idx = newHead - t;
            histIndices[t] = (idx >= 0) ? idx : idx + TRAIL_LENGTH;
        }

        // Calculate valid trail count
        int validTrailCount = polygon.historyFull ? TRAIL_LENGTH : (newHead + 1);

        // Update line positions
        for (int t = 0; t < TRAIL_LENGTH; t++) {
            auto& trailLines = polygon.lines[t];

            // Hide lines beyond valid history
            if (t >= validTrailCount) {
                for (int e = 0; e < NUM_VERTICES; e++) {
                    if (trailLines[e]) {
                        lv_obj_add_flag(trailLines[e], LV_OBJ_FLAG_HIDDEN);
                    }
                }
                continue;
            }

            int histIndex = histIndices[t];
            auto& hist = polygon.history[histIndex];
            auto& linePointsTrail = polygon.linePoints[t];

            for (int e = 0; e < NUM_VERTICES; e++) {
                if (!trailLines[e]) continue;

                lv_obj_remove_flag(trailLines[e], LV_OBJ_FLAG_HIDDEN);

                int v2 = (e + 1) % NUM_VERTICES;

                linePointsTrail[e][0].x = hist[e].x;
                linePointsTrail[e][0].y = hist[e].y;
                linePointsTrail[e][1].x = hist[v2].x;
                linePointsTrail[e][1].y = hist[v2].y;

                lv_line_set_points(trailLines[e], linePointsTrail[e].data(), 2);
            }
        }
    }
}

void MystifyScreensaver::initPolygon(Polygon& polygon, lv_obj_t* parent, lv_coord_t screenW, lv_coord_t screenH) {
    // Sanity check - avoid undefined behavior from modulo by zero
    if (screenW <= 0 || screenH <= 0) return;

    // Pick a random color from the pool
    uint32_t colorHex = COLOR_POOL[rand() % COLOR_POOL.size()];
    polygon.color = lv_color_hex(colorHex);
    polygon.historyHead = 0;
    polygon.historyFull = false;
    // Stagger color changes so polygons don't all change at once
    polygon.colorChangeCounter = rand() % COLOR_CHANGE_INTERVAL;

    // Initialize vertices with random positions and velocities
    for (int v = 0; v < NUM_VERTICES; v++) {
        auto& vertex = polygon.vertices[v];
        vertex.x = static_cast<float>(rand() % screenW);
        vertex.y = static_cast<float>(rand() % screenH);

        // Slower speeds (0.8-2.0) for smooth movement
        vertex.dx = randomFloat(0.8f, 2.0f);
        vertex.dy = randomFloat(0.8f, 2.0f);
        if (rand() % 2) vertex.dx = -vertex.dx;
        if (rand() % 2) vertex.dy = -vertex.dy;

        // Ensure dx != dy for more interesting movement patterns
        if (std::fabs(std::fabs(vertex.dx) - std::fabs(vertex.dy)) < 0.3f) {
            vertex.dy += (vertex.dy > 0 ? 0.5f : -0.5f);
        }
    }

    // Initialize history with current positions
    for (int t = 0; t < TRAIL_LENGTH; t++) {
        for (int v = 0; v < NUM_VERTICES; v++) {
            polygon.history[t][v].x = polygon.vertices[v].x;
            polygon.history[t][v].y = polygon.vertices[v].y;
        }
    }

    // Create line objects with pre-computed opacity values
    for (int t = 0; t < TRAIL_LENGTH; t++) {
        int opacity = LV_OPA_COVER - (t * LV_OPA_COVER / TRAIL_LENGTH);
        if (opacity < LV_OPA_10) opacity = LV_OPA_10;

        for (int e = 0; e < NUM_VERTICES; e++) {
            lv_obj_t* line = lv_line_create(parent);
            if (line == nullptr) {
                polygon.lines[t][e] = nullptr;
                continue;
            }
            lv_obj_remove_style_all(line);

            lv_obj_set_style_line_color(line, polygon.color, 0);
            lv_obj_set_style_line_opa(line, opacity, 0);
            lv_obj_set_style_line_width(line, 2, 0);

            polygon.lines[t][e] = line;
        }
    }
}

void MystifyScreensaver::updateLineColors(Polygon& polygon) {
    // Update all line colors when color changes
    for (int t = 0; t < TRAIL_LENGTH; t++) {
        for (int e = 0; e < NUM_VERTICES; e++) {
            if (polygon.lines[t][e]) {
                lv_obj_set_style_line_color(polygon.lines[t][e], polygon.color, 0);
            }
        }
    }
}

} // namespace tt::service::displayidle
