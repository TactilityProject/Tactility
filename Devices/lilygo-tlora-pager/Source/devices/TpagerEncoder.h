#pragma once

#include <Tactility/hal/encoder/EncoderDevice.h>
#include <driver/pulse_cnt.h>

class TpagerEncoder final : public tt::hal::encoder::EncoderDevice {

    lv_indev_t* encHandle = nullptr;
    pcnt_unit_handle_t encPcntUnit = nullptr;

    bool initEncoder();
    bool deinitEncoder();

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    TpagerEncoder() {}
    ~TpagerEncoder() override {}

    std::string getName() const override { return "T-Lora Pager Encoder"; }
    std::string getDescription() const override { return "The encoder wheel next to the display"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    int getEncoderPulses() const;

    lv_indev_t* getLvglIndev() override { return encHandle; }
};
