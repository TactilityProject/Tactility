#pragma once

#include <Tactility/MessageQueue.h>
#include <Tactility/PubSub.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/loader/Loader.h>

#include <cstdio>
#include <lvgl.h>

namespace tt::service::gui {

constexpr auto GUI_THREAD_FLAG_DRAW = (1 << 0);
constexpr auto GUI_THREAD_FLAG_INPUT = (1 << 1);
constexpr auto GUI_THREAD_FLAG_EXIT = (1 << 2);
constexpr auto GUI_THREAD_FLAG_ALL = (GUI_THREAD_FLAG_DRAW | GUI_THREAD_FLAG_INPUT | GUI_THREAD_FLAG_EXIT);

/**
 * Output a log warning if the current task is the GUI task.
 * This is meant for code that should either create their own task or use a different task to execute on.
 * @param[in] context a descriptive name or label that refers to the caller of this function
 */
void warnIfRunningOnGuiTask(const char* context);

class GuiService final : public Service {

    // Thread and lock
    Thread* thread = nullptr;
    EventGroup threadFlags;
    RecursiveMutex mutex;
    PubSub<loader::LoaderService::Event>::SubscriptionHandle loader_pubsub_subscription = nullptr;

    // Layers and Canvas
    lv_obj_t* appRootWidget = nullptr;
    lv_obj_t* statusbarWidget = nullptr;

    // App-specific
    std::shared_ptr<app::AppInstance> appToRender = nullptr;

    lv_obj_t* _Nullable keyboard = nullptr;
    lv_group_t* keyboardGroup = nullptr;

    bool isStarted = false;

    static int32_t guiMain();

    void onLoaderEvent(loader::LoaderService::Event event);

    lv_obj_t* createAppViews(lv_obj_t* parent);

    void redraw();

    void lock() const {
        check(mutex.lock(pdMS_TO_TICKS(1000)));
    }

    void unlock() const {
        mutex.unlock();
    }

    void showApp(std::shared_ptr<app::AppInstance> app);

    void hideApp();

public:

    bool onStart(ServiceContext& service) override;

    void onStop(ServiceContext& service) override;

    void requestDraw();

    /**
     * Show the on-screen keyboard.
     * @param[in] textarea the textarea to focus the input for
     */
    void softwareKeyboardShow(lv_obj_t* textarea);

    /**
     * Hide the on-screen keyboard.
     * Has no effect when the keyboard is not visible.
     */
    void softwareKeyboardHide();

    /**
     * The on-screen keyboard is only shown when both of these conditions are true:
     *  - there is no hardware keyboard
     *  - TT_CONFIG_FORCE_ONSCREEN_KEYBOARD is set to true in tactility_config.h
     * @return if we should show a on-screen keyboard for text input inside our apps
     */
    bool softwareKeyboardIsEnabled();

    /**
     * Glue code for the on-screen keyboard and the hardware keyboard:
     *  - Attach automatic hide/show parameters for the on-screen keyboard.
     *  - Registers the textarea to the default lv_group_t for hardware keyboards.
     * @param[in] textarea
     */
    void keyboardAddTextArea(lv_obj_t* textarea);

};

std::shared_ptr<GuiService> findService();

} // namespace
