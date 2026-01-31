#pragma once

#include <tactility/hal/Device.h>
#include <lvgl.h>

class TrackballDevice : public tt::hal::Device {
public:
    tt::hal::Device::Type getType() const override { return tt::hal::Device::Type::Other; }
    std::string getName() const override { return "Trackball"; }
    std::string getDescription() const override { return "5-way GPIO trackball navigation"; }
    
    bool start();
    bool stop();
    bool isAttached() const { return initialized; }
    
    lv_indev_t* getLvglIndev() const { return indev; }
    
private:
    lv_indev_t* indev = nullptr;
    bool initialized = false;
};
