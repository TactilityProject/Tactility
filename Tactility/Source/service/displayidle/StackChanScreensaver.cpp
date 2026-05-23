#ifdef ESP_PLATFORM

#include "StackChanScreensaver.h"
#include <algorithm>

namespace tt::service::displayidle {

static lv_obj_t* makeFacePart(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_coord_t offX, lv_coord_t offY, bool circle) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_radius(obj, circle ? LV_RADIUS_CIRCLE : 0, 0);
    lv_obj_align(obj, LV_ALIGN_CENTER, offX, offY);
    return obj;
}

void StackChanScreensaver::start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) {
    // Scale face to ~12% of the shorter screen dimension, preserving 4:3 aspect ratio
    lv_coord_t shorter = (screenW < screenH) ? screenW : screenH;
    logoH_ = shorter / 8;
    logoW_ = logoH_ * 4 / 3;

    // Face part sizes scaled proportionally to original (64x48 base with 6px eyes, 18x2 mouth)
    lv_coord_t eyeSize   = std::max<lv_coord_t>(2, logoH_ / 8);
    lv_coord_t eyeOffX   = logoW_ / 4;
    lv_coord_t eyeOffY   = -(logoH_ / 10);
    lv_coord_t mouthW    = logoW_ * 18 / 64;
    lv_coord_t mouthH    = std::max<lv_coord_t>(2, logoH_ / 24);
    lv_coord_t mouthOffY = logoH_ / 10;

    // Speed: ~5% of shorter dimension per second at 50ms tick = 0.25% per tick
    lv_coord_t speed = std::max<lv_coord_t>(2, shorter / 80);

    logo_ = lv_obj_create(overlay);
    lv_obj_remove_style_all(logo_);
    lv_obj_set_size(logo_, logoW_, logoH_);
    lv_obj_set_style_bg_opa(logo_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(logo_, lv_color_hex(COLORS[0]), 0);
    lv_obj_set_style_radius(logo_, 0, 0);
    lv_obj_clear_flag(logo_, LV_OBJ_FLAG_SCROLLABLE);

    leftEye_  = makeFacePart(logo_, eyeSize, eyeSize, -eyeOffX, eyeOffY, true);
    rightEye_ = makeFacePart(logo_, eyeSize, eyeSize,  eyeOffX, eyeOffY, true);
    mouth_    = makeFacePart(logo_, mouthW, mouthH, 0, mouthOffY, false);

    x_ = (screenW - logoW_) / 2;
    y_ = (screenH - logoH_) / 2;
    dx_ = speed;
    dy_ = speed;
    colorIndex_ = 0;

    lv_obj_set_pos(logo_, x_, y_);
}

void StackChanScreensaver::stop() {
    logo_     = nullptr;
    leftEye_  = nullptr;
    rightEye_ = nullptr;
    mouth_    = nullptr;
}

void StackChanScreensaver::update(lv_coord_t screenW, lv_coord_t screenH) {
    if (!logo_) return;

    x_ += dx_;
    y_ += dy_;

    bool collided = false;

    if (x_ <= 0) {
        x_ = 0;
        dx_ = -dx_;
        collided = true;
    } else if (x_ >= screenW - logoW_) {
        x_ = screenW - logoW_;
        dx_ = -dx_;
        collided = true;
    }

    if (y_ <= 0) {
        y_ = 0;
        dy_ = -dy_;
        collided = true;
    } else if (y_ >= screenH - logoH_) {
        y_ = screenH - logoH_;
        dy_ = -dy_;
        collided = true;
    }

    if (collided) {
        colorIndex_ = (colorIndex_ + 1) % static_cast<int>(COLORS.size());
        lv_obj_set_style_bg_color(logo_, lv_color_hex(COLORS[colorIndex_]), 0);
    }

    lv_obj_set_pos(logo_, x_, y_);
}

} // namespace tt::service::displayidle

#endif // ESP_PLATFORM
