#ifdef ESP_PLATFORM

#include "BouncingBallsScreensaver.h"
#include <cstdlib>

namespace tt::service::displayidle {

void BouncingBallsScreensaver::start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) {
    for (int i = 0; i < NUM_BALLS; i++) {
        initBall(balls_[i], overlay, screenW, screenH, i);
    }
}

void BouncingBallsScreensaver::stop() {
    for (auto& ball : balls_) {
        ball.obj = nullptr;  // Objects deleted by parent overlay
    }
}

void BouncingBallsScreensaver::update(lv_coord_t screenW, lv_coord_t screenH) {
    for (auto& ball : balls_) {
        if (!ball.obj) continue;

        ball.x += ball.dx;
        ball.y += ball.dy;

        bool bounced = false;

        if (ball.x <= 0) {
            ball.x = 0;
            ball.dx = -ball.dx;
            bounced = true;
        } else if (ball.x >= screenW - BALL_SIZE) {
            ball.x = screenW - BALL_SIZE;
            ball.dx = -ball.dx;
            bounced = true;
        }

        if (ball.y <= 0) {
            ball.y = 0;
            ball.dy = -ball.dy;
            bounced = true;
        } else if (ball.y >= screenH - BALL_SIZE) {
            ball.y = screenH - BALL_SIZE;
            ball.dy = -ball.dy;
            bounced = true;
        }

        if (bounced) {
            setRandomTargetColor(ball);
        }

        updateBallColor(ball);
        lv_obj_set_pos(ball.obj, ball.x, ball.y);
    }
}

void BouncingBallsScreensaver::initBall(Ball& ball, lv_obj_t* parent, lv_coord_t screenW, lv_coord_t screenH, int index) {
    ball.obj = lv_obj_create(parent);
    if (ball.obj == nullptr) {
        return;
    }
    lv_obj_remove_style_all(ball.obj);
    lv_obj_set_size(ball.obj, BALL_SIZE, BALL_SIZE);
    lv_obj_set_style_bg_opa(ball.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ball.obj, BALL_SIZE / 2, 0);

    ball.x = (screenW > BALL_SIZE) ? rand() % (screenW - BALL_SIZE) : 0;
    ball.y = (screenH > BALL_SIZE) ? rand() % (screenH - BALL_SIZE) : 0;

    // Random speeds between 1-4 with random direction
    // Note: adjustment below may extend range to 1-5 to ensure varied paths
    ball.dx = (rand() % 4) + 1;
    ball.dy = (rand() % 4) + 1;
    if (rand() % 2) ball.dx = -ball.dx;
    if (rand() % 2) ball.dy = -ball.dy;
    // Ensure dx != dy for more varied paths (may adjust dy to 5 or -5)
    if (abs(ball.dx) == abs(ball.dy)) {
        ball.dy = ball.dy > 0 ? ball.dy + 1 : ball.dy - 1;
    }

    uint32_t color = COLORS[index % COLORS.size()];
    ball.currentR = ball.targetR = (color >> 16) & 0xFF;
    ball.currentG = ball.targetG = (color >> 8) & 0xFF;
    ball.currentB = ball.targetB = color & 0xFF;
    ball.colorStep = 0;

    lv_obj_set_style_bg_color(ball.obj, lv_color_make(ball.currentR, ball.currentG, ball.currentB), 0);
    lv_obj_set_pos(ball.obj, ball.x, ball.y);
}

void BouncingBallsScreensaver::setRandomTargetColor(Ball& ball) {
    uint32_t color = COLORS[rand() % COLORS.size()];
    ball.targetR = (color >> 16) & 0xFF;
    ball.targetG = (color >> 8) & 0xFF;
    ball.targetB = color & 0xFF;
    ball.colorStep = COLOR_FADE_STEPS;
}

void BouncingBallsScreensaver::updateBallColor(Ball& ball) {
    if (ball.colorStep > 0) {
        int stepR = (static_cast<int>(ball.targetR) - ball.currentR) / ball.colorStep;
        int stepG = (static_cast<int>(ball.targetG) - ball.currentG) / ball.colorStep;
        int stepB = (static_cast<int>(ball.targetB) - ball.currentB) / ball.colorStep;

        ball.currentR = static_cast<uint8_t>(ball.currentR + stepR);
        ball.currentG = static_cast<uint8_t>(ball.currentG + stepG);
        ball.currentB = static_cast<uint8_t>(ball.currentB + stepB);
        ball.colorStep--;

        if (ball.colorStep == 0) {
            ball.currentR = ball.targetR;
            ball.currentG = ball.targetG;
            ball.currentB = ball.targetB;
        }

        if (ball.obj) {
            lv_obj_set_style_bg_color(ball.obj, lv_color_make(ball.currentR, ball.currentG, ball.currentB), 0);
        }
    }
}

} // namespace tt::service::displayidle

#endif // ESP_PLATFORM
