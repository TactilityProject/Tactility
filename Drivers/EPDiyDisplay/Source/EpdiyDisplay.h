#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/Lock.h>
#include <Tactility/Mutex.h>

#include <epdiy.h>
#include <epd_highlevel.h>
#include <lvgl.h>
#include <memory>

class EpdiyDisplay final : public tt::hal::display::DisplayDevice {

public:

    class Configuration {
    public:
        Configuration(
            const EpdBoardDefinition* board,
            const EpdDisplay_t* display,
            std::shared_ptr<tt::hal::touch::TouchDevice> touch = nullptr,
            enum EpdInitOptions initOptions = EPD_OPTIONS_DEFAULT,
            const EpdWaveform* waveform = EPD_BUILTIN_WAVEFORM,
            int defaultTemperature = 25,
            enum EpdDrawMode defaultDrawMode = MODE_GL16,
            bool fullRefresh = false,
            enum EpdRotation rotation = EPD_ROT_LANDSCAPE
        ) : board(board),
            display(display),
            touch(std::move(touch)),
            initOptions(initOptions),
            waveform(waveform),
            defaultTemperature(defaultTemperature),
            defaultDrawMode(defaultDrawMode),
            fullRefresh(fullRefresh),
            rotation(rotation)
        {}

        const EpdBoardDefinition* board;
        const EpdDisplay_t* display;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        enum EpdInitOptions initOptions;
        const EpdWaveform* waveform;
        int defaultTemperature;
        enum EpdDrawMode defaultDrawMode;
        bool fullRefresh;
        enum EpdRotation rotation;
    };

private:
    std::unique_ptr<Configuration> configuration;
    std::shared_ptr<tt::Mutex> lock;
    lv_display_t* _Nullable lvglDisplay = nullptr;
    EpdiyHighlevelState highlevelState = {};
    uint8_t* framebuffer = nullptr;
    uint8_t* lvglDrawBuffer = nullptr;
    bool initialized = false;
    bool powered = false;

    static void flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap);
    void flushInternal(const lv_area_t* area, uint8_t* pixelMap);
    
    static void rotationEventCallback(lv_event_t* event);
    void handleRotationChange(lv_display_rotation_t rotation);
    
    // Rotation mapping helpers
    static lv_display_rotation_t epdRotationToLvgl(enum EpdRotation epdRotation);
    static enum EpdRotation lvglRotationToEpd(lv_display_rotation_t lvglRotation);

public:

    explicit EpdiyDisplay(std::unique_ptr<Configuration> inConfiguration);
    
    ~EpdiyDisplay() override;

    std::string getName() const override { return "EPDiy"; }
    
    std::string getDescription() const override { 
        return "E-Ink display powered by EPDiy library"; 
    }

    // Device lifecycle
    bool start() override;
    bool stop() override;

    // Power control
    void setPowerOn(bool turnOn) override;
    bool isPoweredOn() const override { return powered; }
    bool supportsPowerControl() const override { return true; }

    // Touch device
    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override { 
        return configuration->touch; 
    }

    // LVGL support
    bool supportsLvgl() const override { return true; }
    bool startLvgl() override;
    bool stopLvgl() override;
    lv_display_t* _Nullable getLvglDisplay() const override { return lvglDisplay; }

    // DisplayDriver (not supported for EPD)
    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable getDisplayDriver() override { 
        return nullptr; 
    }

    // EPD specific functions
    
    /**
     * Get a reference to the framebuffer
     */
    uint8_t* getFramebuffer() { 
        return epd_hl_get_framebuffer(&highlevelState); 
    }
    
    /**
     * Clear the screen by flashing it
     */
    void clearScreen(); 
    
    /**
     * Clear an area by flashing it
     */
    void clearArea(EpdRect area);
    
    /**
     * Manually trigger a screen update
     * @param mode The draw mode to use (defaults to configuration default)
     * @param temperature Temperature in °C (defaults to configuration default)
     */
    enum EpdDrawError updateScreen(
        enum EpdDrawMode mode = MODE_UNKNOWN_WAVEFORM, 
        int temperature = -1
    );
    
    /**
     * Update a specific area of the screen
     * @param area The area to update
     * @param mode The draw mode to use (defaults to configuration default)
     * @param temperature Temperature in °C (defaults to configuration default)
     */
    enum EpdDrawError updateArea(
        EpdRect area,
        enum EpdDrawMode mode = MODE_UNKNOWN_WAVEFORM,
        int temperature = -1
    );
    
    /**
     * Set the display to all white
     */
    void setAllWhite();
};
