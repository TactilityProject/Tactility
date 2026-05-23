#pragma once
#ifdef ESP_PLATFORM

#include "Screensaver.h"
#include <array>
#include <cstdint>

namespace tt::service::displayidle {

class StackChanScreensaver final : public Screensaver {
public:
    StackChanScreensaver() = default;
    ~StackChanScreensaver() override = default;
    StackChanScreensaver(const StackChanScreensaver&) = delete;
    StackChanScreensaver& operator=(const StackChanScreensaver&) = delete;
    StackChanScreensaver(StackChanScreensaver&&) = delete;
    StackChanScreensaver& operator=(StackChanScreensaver&&) = delete;

    void start(lv_obj_t* overlay, lv_coord_t screenW, lv_coord_t screenH) override;
    void stop() override;
    void update(lv_coord_t screenW, lv_coord_t screenH) override;

private:
    static constexpr std::array<uint32_t, 8> COLORS = {
        0xffffff, 0xfffa01, 0xff8300, 0x00feff,
        0xff2600, 0xbe00ff, 0x0026ff, 0xff008b,
    };
    static_assert(COLORS.size() > 0);

    lv_obj_t* logo_     = nullptr;
    lv_obj_t* leftEye_  = nullptr;
    lv_obj_t* rightEye_ = nullptr;
    lv_obj_t* mouth_    = nullptr;

    lv_coord_t logoW_ = 0;
    lv_coord_t logoH_ = 0;
    lv_coord_t x_     = 0;
    lv_coord_t y_     = 0;
    lv_coord_t dx_    = 0;
    lv_coord_t dy_    = 0;
    int colorIndex_   = 0;
};

} // namespace tt::service::displayidle

#endif // ESP_PLATFORM
