#include "MatrixRainScreensaver.h"
#include <cstdlib>
#include <algorithm>
#include <cassert>

namespace tt::service::displayidle {

static_assert(MatrixRainScreensaver::MAX_TRAIL_LENGTH > MatrixRainScreensaver::MIN_TRAIL_LENGTH,
              "MAX_TRAIL_LENGTH must be greater than MIN_TRAIL_LENGTH");
static_assert(MatrixRainScreensaver::MAX_TICK_DELAY >= MatrixRainScreensaver::MIN_TICK_DELAY,
              "MAX_TICK_DELAY must be >= MIN_TICK_DELAY");

static constexpr int TRAIL_LENGTH_RANGE = MatrixRainScreensaver::MAX_TRAIL_LENGTH - MatrixRainScreensaver::MIN_TRAIL_LENGTH;
static constexpr int TICK_DELAY_RANGE = MatrixRainScreensaver::MAX_TICK_DELAY - MatrixRainScreensaver::MIN_TICK_DELAY + 1;

char MatrixRainScreensaver::randomChar() {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*<>?";
    return chars[rand() % (sizeof(chars) - 1)];
}

lv_color_t MatrixRainScreensaver::getTrailColor(int index, int trailLength) const {
    if (trailLength <= 1) {
        return lv_color_hex(COLOR_GRADIENT[0]);
    }

    float intensity = static_cast<float>(index) / static_cast<float>(trailLength - 1);
    int colorIndex = static_cast<int>(intensity * (COLOR_GRADIENT.size() - 1) + 0.5f);

    if (colorIndex < 0) colorIndex = 0;
    if (colorIndex >= static_cast<int>(COLOR_GRADIENT.size())) {
        colorIndex = COLOR_GRADIENT.size() - 1;
    }

    return lv_color_hex(COLOR_GRADIENT[colorIndex]);
}

int MatrixRainScreensaver::getRandomAvailableColumn() {
    if (availableColumns_.empty()) {
        return -1;
    }
    int idx = rand() % availableColumns_.size();
    int column = availableColumns_[idx];
    availableColumns_.erase(availableColumns_.begin() + idx);
    return column;
}

void MatrixRainScreensaver::releaseColumn(int column) {
    // Add column back to available pool if not already present (duplicate protection)
    bool alreadyPresent = std::find(availableColumns_.begin(), availableColumns_.end(), column) != availableColumns_.end();
    assert(!alreadyPresent && "Column released twice - indicates bug in column management");
    if (!alreadyPresent) {
        availableColumns_.push_back(column);
    }
}

void MatrixRainScreensaver::createRainCharLabels(RainChar& rc, int trailIndex, int trailLength) {
    rc.ch = randomChar();

    // Create glow layer first (behind main text)
    rc.glowLabel = lv_label_create(overlay_);
    lv_obj_set_style_text_font(rc.glowLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rc.glowLabel, lv_color_hex(COLOR_GLOW), 0);
    lv_obj_set_style_opa(rc.glowLabel, LV_OPA_70, 0);

    // Create main character label
    rc.label = lv_label_create(overlay_);
    lv_obj_set_style_text_font(rc.label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rc.label, getTrailColor(trailIndex, trailLength), 0);

    char text[2] = {rc.ch, '\0'};
    lv_label_set_text(rc.label, text);
    lv_label_set_text(rc.glowLabel, text);

    // Hide initially
    lv_obj_set_pos(rc.label, -100, -100);
    lv_obj_set_pos(rc.glowLabel, -100, -100);
}

void MatrixRainScreensaver::initRaindrop(Raindrop& drop, int column) {
    drop.column = column;
    drop.trailLength = MIN_TRAIL_LENGTH + rand() % TRAIL_LENGTH_RANGE;
    drop.headRow = -(drop.trailLength);  // Start above screen
    drop.tickDelay = MIN_TICK_DELAY + rand() % TICK_DELAY_RANGE;
    drop.tickCounter = 0;
    drop.active = true;

    // Create character labels for trail
    drop.chars.resize(drop.trailLength);
    for (int i = 0; i < drop.trailLength; i++) {
        createRainCharLabels(drop.chars[i], i, drop.trailLength);
    }
}

void MatrixRainScreensaver::resetRaindrop(Raindrop& drop) {
    releaseColumn(drop.column);

    int newColumn = getRandomAvailableColumn();
    if (newColumn < 0) {
        drop.active = false;
        return;
    }

    drop.column = newColumn;
    drop.tickDelay = MIN_TICK_DELAY + rand() % TICK_DELAY_RANGE;
    drop.tickCounter = 0;

    int newTrailLength = MIN_TRAIL_LENGTH + rand() % TRAIL_LENGTH_RANGE;

    // Resize if needed
    if (newTrailLength != drop.trailLength) {
        // Delete excess labels
        for (size_t i = newTrailLength; i < drop.chars.size(); i++) {
            if (drop.chars[i].label) {
                lv_obj_delete(drop.chars[i].label);
            }
            if (drop.chars[i].glowLabel) {
                lv_obj_delete(drop.chars[i].glowLabel);
            }
        }

        size_t oldSize = drop.chars.size();
        drop.chars.resize(newTrailLength);

        // Create new labels if needed (will be refreshed below)
        for (size_t i = oldSize; i < drop.chars.size(); i++) {
            createRainCharLabels(drop.chars[i], static_cast<int>(i), newTrailLength);
        }

        drop.trailLength = newTrailLength;
    }

    // Reset position above screen
    drop.headRow = -(drop.trailLength);

    // Refresh characters and colors for all labels
    for (int i = 0; i < drop.trailLength; i++) {
        auto& rc = drop.chars[i];
        rc.ch = randomChar();

        char text[2] = {rc.ch, '\0'};
        lv_label_set_text(rc.label, text);
        lv_label_set_text(rc.glowLabel, text);
        lv_obj_set_style_text_color(rc.label, getTrailColor(i, drop.trailLength), 0);

        lv_obj_set_pos(rc.label, -100, -100);
        lv_obj_set_pos(rc.glowLabel, -100, -100);
    }

    drop.active = true;
}

void MatrixRainScreensaver::updateRaindropDisplay(Raindrop& drop) {
    int xPos = drop.column * CHAR_WIDTH;

    for (int i = 0; i < drop.trailLength; i++) {
        auto& rc = drop.chars[i];
        int row = drop.headRow - i;
        int yPos = row * CHAR_HEIGHT;

        // Only show characters that are on screen (grid-snapped positions)
        if (row >= 0 && row < numRows_) {
            lv_obj_set_pos(rc.label, xPos, yPos);
            lv_obj_set_pos(rc.glowLabel, xPos + GLOW_OFFSET, yPos + GLOW_OFFSET);
        } else {
            lv_obj_set_pos(rc.label, -100, -100);
            lv_obj_set_pos(rc.glowLabel, -100, -100);
        }
    }
}

void MatrixRainScreensaver::flickerRandomChars() {
    constexpr int ERROR_PERCENT = 3;

    for (auto& drop : drops_) {
        if (!drop.active) continue;

        for (int i = 0; i < drop.trailLength; i++) {
            int row = drop.headRow - i;

            // Only process visible characters
            if (row >= 0 && row < numRows_) {
                if (rand() % 100 < ERROR_PERCENT) {
                    auto& rc = drop.chars[i];
                    rc.ch = randomChar();
                    char text[2] = {rc.ch, '\0'};
                    lv_label_set_text(rc.label, text);
                    lv_label_set_text(rc.glowLabel, text);
                }
            }
        }
    }
}

void MatrixRainScreensaver::start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) {
    if (!drops_.empty()) {
        stop();
    }

    overlay_ = overlay;
    screenW_ = screenW;
    screenH_ = screenH;
    globalFlickerCounter_ = 0;

    numColumns_ = screenW / CHAR_WIDTH;
    numRows_ = screenH / CHAR_HEIGHT;

    availableColumns_.clear();
    for (int i = 0; i < numColumns_; i++) {
        availableColumns_.push_back(i);
    }

    int numDrops = std::min(MAX_ACTIVE_DROPS, numColumns_);
    drops_.resize(numDrops);

    for (int i = 0; i < numDrops; i++) {
        int col = getRandomAvailableColumn();
        if (col >= 0) {
            initRaindrop(drops_[i], col);
            // Stagger start positions
            drops_[i].headRow = -(rand() % (numRows_ / 2));
        }
    }
}

void MatrixRainScreensaver::stop() {
    // Explicitly delete labels to prevent resource leaks
    for (auto& drop : drops_) {
        for (auto& rc : drop.chars) {
            if (rc.label) {
                lv_obj_delete(rc.label);
                rc.label = nullptr;
            }
            if (rc.glowLabel) {
                lv_obj_delete(rc.glowLabel);
                rc.glowLabel = nullptr;
            }
        }
        drop.chars.clear();
    }
    drops_.clear();
    availableColumns_.clear();
    overlay_ = nullptr;
}

void MatrixRainScreensaver::update(lv_coord_t screenW, lv_coord_t screenH) {
    LV_UNUSED(screenW);
    LV_UNUSED(screenH);
    // Global glitch effect
    globalFlickerCounter_++;
    if (globalFlickerCounter_ >= 5) {
        globalFlickerCounter_ = 0;
        flickerRandomChars();
    }

    for (auto& drop : drops_) {
        if (!drop.active) continue;

        // Tick-based movement (terminal style - advance by row)
        drop.tickCounter++;
        if (drop.tickCounter >= drop.tickDelay) {
            drop.tickCounter = 0;

            // Advance head down by one row
            drop.headRow++;

            // Randomly change the head character when it advances
            if (!drop.chars.empty()) {
                drop.chars[0].ch = randomChar();
                char text[2] = {drop.chars[0].ch, '\0'};
                lv_label_set_text(drop.chars[0].label, text);
                lv_label_set_text(drop.chars[0].glowLabel, text);
            }

            // Check if tail has completely left the screen
            int tailRow = drop.headRow - (drop.trailLength - 1);
            if (tailRow >= numRows_) {
                resetRaindrop(drop);
                continue;
            }
        }

        updateRaindropDisplay(drop);
    }
}

} // namespace tt::service::displayidle
