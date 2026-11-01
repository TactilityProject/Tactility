#pragma once

#include <Tactility/app/timezone/TimeZoneView.h>
#include <Tactility/app/wifimanage/WifiManagePrivate.h>

#include <tactility/device.h>
#include <tactility/drivers/wifi.h>

#include <lvgl/fonts.h>
#include <lvgl.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tt::app::setup {

enum class Phase {
    Welcome,
    StepIntro,
    /** The choices of a step that has an intro page, see StepConfiguration::choiceTitle */
    StepChoice,
    /** The Wi-Fi network list, shown after the intro page of the Wi-Fi step */
    WifiSetup,
    Done
};

struct StepConfiguration {
    std::string title;
    std::string description;
    std::function<void()> run;
    /** Optional: shows the page that the step ends on, when coming back from the next step. Called on the LVGL thread. */
    std::function<void()> showLast = nullptr;
    /** Optional: handles the result of the dialog that run() started */
    std::function<void(int32_t)> onResult = nullptr;
    /** What the page shows below its title */
    enum class Content {
        Description,
        /** The description, with the choices below it */
        Choice,
        /** The time zone list instead of the description */
        TimeZones,
        /** The Wi-Fi network list instead of the description, only used by Phase::WifiSetup */
        Wifi
    } content = Content::Description;
    /** Optional: the title of a separate page with the choices, shown after the intro page that holds the description */
    std::string choiceTitle;
    /** The labels of the chips of a Content::Choice page */
    std::vector<std::string> choices;
    /** The index in choices that is picked, -1 when none. Guarded by the LVGL lock. */
    int32_t choice = -1;
    /** Optional: the choice to pick when the page is shown and none was picked yet. Called with the LVGL lock held. */
    std::function<int32_t()> defaultChoice = nullptr;
};

struct Context {
    uint32_t appInstanceId;

    Phase phase = Phase::Welcome;
    size_t stepIndex = 0;
    std::vector<StepConfiguration> steps;
    uint32_t pendingStepDialogId = 0;

    // Valid while the window is shown
    // The area above the buttons, which scrolls when the page doesn't fit
    lv_obj_t* content = nullptr;
    lv_obj_t* contentInner = nullptr;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* descriptionLabel = nullptr;
    lv_obj_t* backButton = nullptr;
    lv_obj_t* skipButton = nullptr;
    lv_obj_t* continueButton = nullptr;
    lv_obj_t* stepIndicator = nullptr;
    lv_obj_t* bottom = nullptr;
    lv_obj_t* timeZoneContainer = nullptr;
    lv_obj_t* choiceChips = nullptr;
    lv_obj_t* wifiContainer = nullptr;

    // Guarded by the LVGL lock
    Device* wifiDevice = nullptr;
    WifiEventSubscription wifiEventSub {};
    wifimanage::State wifiState;
    /** The network that was picked last */
    std::string wifiTarget;
    bool wifiConnectFailed = false;
    /** The SSIDs of the list items */
    std::vector<std::string> wifiSsids;
    // Wi-Fi work for the app's task, as it blocks. Guarded by the LVGL lock.
    TaskEventGroup* eventGroup = nullptr;
    uint32_t wifiActionBit = 0;
    bool radioOnRequested = false;
    std::string connectRequest;

    std::unique_ptr<timezone::TimeZoneView> timeZoneView;
    // The time zone picked on the time zone page, guarded by the LVGL lock
    std::string timeZoneName;
    std::string timeZoneCode;
};

// SetupCompletion.cpp

void markCompleted();

// SetupLayout.cpp

uint32_t getStepCount(const Context* ctx);
enum LvglFontSize getTitleFontSize();
/** Shows the current page. Requires the LVGL lock. */
void renderCurrent(Context* ctx);

// SetupWifi.cpp

/** Claims the action bit and subscribes to Wi-Fi events, which are signalled on @a eventGroup */
void wifiStart(Context* ctx, TaskEventGroup* eventGroup);
void wifiStop(Context* ctx);
/** Handles the pending requests and events. Call it from the app's task. */
void wifiProcess(Context* ctx);
/** Shows the Wi-Fi page and turns Wi-Fi on */
void enterWifiSetup(Context* ctx);
/** Fills the Wi-Fi container for the current state. Requires the LVGL lock. */
void renderWifi(Context* ctx);

} // namespace
