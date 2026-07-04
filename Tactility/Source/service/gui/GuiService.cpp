#include <Tactility/service/gui/GuiService.h>

#include <cstring>

#include <Tactility/app/AppInstance.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/lvgl/UsbHidInput.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/Tactility.h>

#include <tactility/log.h>

namespace tt::service::gui {

extern const ServiceManifest manifest;
constexpr auto* TAG = "GuiService";
using namespace loader;

constexpr auto* GUI_TASK_NAME = "gui";

void warnIfRunningOnGuiTask(const char* context) {
    const char* task_name = pcTaskGetName(nullptr);
    if (strcmp(GUI_TASK_NAME, task_name) == 0) {
        LOG_W(TAG, "%s shouldn't run on the GUI task", context);
    }
}

namespace {

enum class GuiDispatchType { Show, Hide, Exit };

struct GuiDispatchItem {
    GuiService* service;
    GuiDispatchType type;
    std::shared_ptr<app::AppInstance> appInstance; // only used for Show
};

} // namespace

// region AppManifest

void GuiService::onGuiDispatch(void* context) {
    std::unique_ptr<GuiDispatchItem> item(static_cast<GuiDispatchItem*>(context));
    switch (item->type) {
        case GuiDispatchType::Show:
            item->service->showApp(item->appInstance);
            break;
        case GuiDispatchType::Hide:
            item->service->hideApp();
            break;
        case GuiDispatchType::Exit:
            item->service->exitRequested = true;
            break;
    }
}

void GuiService::onLoaderEvent(LoaderService::Event event) {
    GuiDispatchItem* item;
    if (event == LoaderService::Event::ApplicationShowing) {
        auto app_instance = std::static_pointer_cast<app::AppInstance>(app::getCurrentAppContext());
        item = new GuiDispatchItem{this, GuiDispatchType::Show, app_instance};
    } else if (event == LoaderService::Event::ApplicationHiding) {
        item = new GuiDispatchItem{this, GuiDispatchType::Hide, nullptr};
    } else {
        return;
    }

    if (dispatcher_dispatch(dispatcher, item, onGuiDispatch) != ERROR_NONE) {
        LOG_E(TAG, "Failed to dispatch gui event");
        delete item;
    }
}

int32_t GuiService::guiMain() {
    auto service = findServiceById<GuiService>(manifest.id);

    if (!lvgl::lock(5000)) {
        LOG_E(TAG, "LVGL guiMain start failed as LVGL couldn't be locked");
        return 0;
    }

    // The screen root is created in the main task instead of during onStart because
    // it allows onStart() to succeed faster and allows widget creation to happen in the background

    auto* screen_root = lv_screen_active();
    if (screen_root == nullptr) {
        LOG_E(TAG, "No display found, exiting GUI task");
        lvgl::unlock();
        return 0;
    }

    service->keyboardGroup = lv_group_create();
    lv_obj_set_style_border_width(screen_root, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(screen_root, 0, LV_STATE_DEFAULT);

    lv_obj_t* vertical_container = lv_obj_create(screen_root);
    lv_obj_set_size(vertical_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(vertical_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(vertical_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(vertical_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(vertical_container, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(vertical_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(vertical_container, 0, LV_STATE_DEFAULT);

    service->statusbarWidget = lvgl::statusbar_create(vertical_container);

    auto* app_container = lv_obj_create(vertical_container);
    lv_obj_set_style_pad_all(app_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(app_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(app_container, LV_PCT(100));
    lv_obj_set_flex_grow(app_container, 1);
    lv_obj_set_flex_flow(app_container, LV_FLEX_FLOW_COLUMN);

    service->appRootWidget = app_container;

    lvgl::unlock();

    while (!service->exitRequested) {
        dispatcher_consume(service->dispatcher);
    }

    service->appRootWidget = nullptr;
    service->statusbarWidget = nullptr;

    return 0;
}

lv_obj_t* GuiService::createAppViews(lv_obj_t* parent) {
    lv_obj_send_event(statusbarWidget, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_t* child_container = lv_obj_create(parent);
    lv_obj_set_style_pad_all(child_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(child_container, LV_PCT(100));
    lv_obj_set_style_border_width(child_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(child_container, 1);

    if (softwareKeyboardIsEnabled()) {
        keyboard = lv_keyboard_create(parent);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    } else {
        keyboard = nullptr;
    }

    return child_container;
}

void GuiService::redraw() {
    // Lock GUI and LVGL
    lock();

    if (appRootWidget == nullptr) {
        LOG_W(TAG, "No root widget");
        unlock();
        return;
    }

    if (lvgl::lock(1000)) {
        lv_obj_clean(appRootWidget);

        if (appToRender != nullptr) {

            // Create a default group which adds all objects automatically,
            // and assign all indevs to it.
            // This enables navigation with limited input, such as encoder wheels.
            lv_group_t* group = lv_group_create();
            auto* indev = lv_indev_get_next(nullptr);
            while (indev) {
                lv_indev_set_group(indev, group);
                indev = lv_indev_get_next(indev);
            }
            lv_group_set_default(group);

            app::Flags flags = std::static_pointer_cast<app::AppInstance>(appToRender)->getFlags();
            if (flags.hideStatusbar) {
                lv_obj_add_flag(statusbarWidget, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(statusbarWidget, LV_OBJ_FLAG_HIDDEN);
            }

            lv_obj_t* container = createAppViews(appRootWidget);
            appToRender->getApp()->onShow(*appToRender, container);
        } else {
            LOG_W(TAG, "Nothing to draw");
        }

        // Unlock GUI and LVGL
        lvgl::unlock();
    } else {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
    }

    unlock();
}

bool GuiService::onStart(ServiceContext& service) {
    exitRequested = false;
    dispatcher = dispatcher_alloc();

    thread = new Thread(
        GUI_TASK_NAME,
        4096, // Last known minimum was 2800 for launching desktop
        guiMain
    );
    thread->setPriority(THREAD_PRIORITY_SERVICE);

    const auto loader = findLoaderService();
    assert(loader != nullptr);
    loader_pubsub_subscription = loader->getPubsub()->subscribe([this](auto event) {
        onLoaderEvent(event);
    });

    isStarted = true;

    lvgl::startUsbHidInput();

    thread->start();

    return true;
}

void GuiService::onStop(ServiceContext& service) {
    lvgl::stopUsbHidInput();

    lock();

    const auto loader = findLoaderService();
    assert(loader != nullptr);
    loader->getPubsub()->unsubscribe(loader_pubsub_subscription);

    appToRender = nullptr;
    isStarted = false;

    unlock();

    auto* exit_item = new GuiDispatchItem{this, GuiDispatchType::Exit, nullptr};
    if (dispatcher_dispatch(dispatcher, exit_item, onGuiDispatch) != ERROR_NONE) {
        LOG_E(TAG, "Failed to dispatch gui exit event");
        check(false, "Failed to dispatch exit signal to thread.");
        delete exit_item;
    }
    thread->join();

    if (lvgl::lock()) {
        if (keyboardGroup != nullptr) {
            lv_group_delete(keyboardGroup);
            keyboardGroup = nullptr;
        }
        lvgl::unlock();
    } else {
        LOG_E(TAG, "Failed to lock LVGL during GUI stop");
    }

    delete thread;
    dispatcher_free(dispatcher);
    dispatcher = nullptr;
}

void GuiService::showApp(std::shared_ptr<app::AppInstance> app) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isStarted) {
        LOG_E(TAG, "Failed to show app %s: GUI not started", app->getManifest().appId.c_str());
        return;
    }

    if (appToRender != nullptr && appToRender->getLaunchId() == app->getLaunchId()) {
        LOG_W(TAG, "Already showing %s", app->getManifest().appId.c_str());
        return;
    }

    LOG_I(TAG, "Showing %s", app->getManifest().appId.c_str());
    // Ensure previous app triggers onHide() logic
    if (appToRender != nullptr) {
        hideApp();
    }

    appToRender = std::move(app);
    redraw();
}

void GuiService::hideApp() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isStarted) {
        LOG_E(TAG, "Failed to hide app: GUI not started");
        return;
    }

    if (appToRender == nullptr) {
        LOG_W(TAG, "hideApp() called but no app is currently shown");
        return;
    }

    // We must lock the LVGL port, because the viewport hide callbacks
    // might call LVGL APIs (e.g. to remove the keyboard from the screen root)
    lvgl::lock(portMAX_DELAY);
    appToRender->getApp()->onHide(*appToRender);
    lvgl::unlock();
    appToRender = nullptr;
}

std::shared_ptr<GuiService> findService() {
    return std::static_pointer_cast<GuiService>(
        findServiceById(manifest.id)
    );
}

extern const ServiceManifest manifest = {
    .id = "Gui",
    .createService = create<GuiService>
};

// endregion

} // namespace
