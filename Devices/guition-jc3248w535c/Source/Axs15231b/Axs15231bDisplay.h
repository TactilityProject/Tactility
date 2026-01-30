#pragma once

#include <Tactility/hal/spi/Spi.h>

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/display/DisplayDriver.h>

#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <lvgl.h>

class Axs15231bDisplay final : public tt::hal::display::DisplayDevice {

public:

    class Configuration {

    public:

        Configuration(
            spi_host_device_t spiHostDevice,
            gpio_num_t csPin,
            gpio_num_t dcPin,
            gpio_num_t resetPin,
            gpio_num_t tePin,
            unsigned int horizontalResolution,
            unsigned int verticalResolution,
            std::shared_ptr<tt::hal::touch::TouchDevice> touch,
            bool swapXY = false,
            bool mirrorX = false,
            bool mirrorY = false,
            bool invertColor = false,
            uint32_t bufferSize = 0, // Size in pixel count. 0 means default, which is 1/10 of the screen size,
            lcd_rgb_element_order_t rgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB
        ) : spiHostDevice(spiHostDevice),
            csPin(csPin),
            dcPin(dcPin),
            resetPin(resetPin),
            tePin(tePin),
            horizontalResolution(horizontalResolution),
            verticalResolution(verticalResolution),
            swapXY(swapXY),
            mirrorX(mirrorX),
            mirrorY(mirrorY),
            invertColor(invertColor),
            bufferSize(bufferSize),
            rgbElementOrder(rgbElementOrder),
            touch(std::move(touch))
        {
            if (this->bufferSize == 0) {
                this->bufferSize = horizontalResolution * verticalResolution / 10;
            }
        }

        spi_host_device_t spiHostDevice;
        gpio_num_t csPin;
        gpio_num_t dcPin;
        gpio_num_t resetPin;
        gpio_num_t tePin;
        unsigned int pixelClockFrequency = 40'000'000; // Hertz
        size_t transactionQueueDepth = 10;
        unsigned int horizontalResolution;
        unsigned int verticalResolution;
        bool swapXY;
        bool mirrorX;
        bool mirrorY;
        bool invertColor;
        uint32_t bufferSize; // Size in pixel count. 0 means default, which is 1/10 of the screen size
        lcd_rgb_element_order_t rgbElementOrder;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        std::function<void(uint8_t)> _Nullable backlightDutyFunction = nullptr;
    };

private:

    esp_lcd_panel_io_handle_t _Nullable ioHandle = nullptr;
    esp_lcd_panel_handle_t _Nullable panelHandle = nullptr;
    lv_display_t* _Nullable lvglDisplay = nullptr;
    uint16_t* _Nullable buffer1 = nullptr;
    uint16_t* _Nullable buffer2 = nullptr;
    uint16_t* _Nullable tempBuf = nullptr;
    SemaphoreHandle_t _Nullable teSyncSemaphore = nullptr;
    bool teIsrInstalled = false;
    bool isrServiceInstalledByUs = false;

    std::unique_ptr<Configuration> configuration;
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable displayDriver;

    bool createIoHandle();

    bool createPanelHandle();

    bool setupTeSync();

    void teardownTeSync();

    static void IRAM_ATTR teIsrHandler(void* arg);

public:

    explicit Axs15231bDisplay(std::unique_ptr<Configuration> inConfiguration);

    std::string getName() const override { return "AXS15231B Display"; }

    std::string getDescription() const override { return "AXS15231B display"; }

    bool start() override;

    bool stop() override;

    bool supportsLvgl() const override { return true; }

    bool startLvgl() override;

    bool stopLvgl() override;

    lv_display_t* _Nullable getLvglDisplay() const override { return lvglDisplay; }

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { return configuration->touch; }

    void setBacklightDuty(uint8_t backlightDuty) override {
        if (configuration->backlightDutyFunction != nullptr) {
            configuration->backlightDutyFunction(backlightDuty);
        }
    }

    bool supportsBacklightDuty() const override { return configuration->backlightDutyFunction != nullptr; }

    bool supportsDisplayDriver() const override { return true; }

    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable getDisplayDriver() override;

    static void lvgl_port_flush_callback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map);

    static bool onColorTransDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
};
