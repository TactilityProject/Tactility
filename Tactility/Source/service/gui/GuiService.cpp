#include <Tactility/service/gui/GuiService.h>

#include <Tactility/app/AppInstance.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/Tactility.h>

namespace tt::service::gui {

extern const ServiceManifest manifest;
static const auto LOGGER = Logger("GuiService");
using namespace loader;

// region AppManifest

void GuiService::onLoaderEvent(LoaderService::Event event) {
    if (event == LoaderService::Event::ApplicationShowing) {
        auto app_instance = std::static_pointer_cast<app::AppInstance>(app::getCurrentAppContext());
        showApp(app_instance);
    } else if (event == LoaderService::Event::ApplicationHiding) {
        hideApp();
    }
}

int32_t GuiService::guiMain() {
    auto service = findServiceById<GuiService>(manifest.id);

    while (true) {
        uint32_t flags = 0;
        if (service->threadFlags.wait(GUI_THREAD_FLAG_ALL, false, true, &flags, portMAX_DELAY)) {
            // When service not started or starting -> exit
            State service_state = getState(manifest.id);
            if (service_state != State::Started && service_state != State::Starting) {
                break;
            }

            // Process and dispatch draw call
            if (flags & GUI_THREAD_FLAG_DRAW) {
                service->threadFlags.clear(GUI_THREAD_FLAG_DRAW);
                service->redraw();
            }

            if (flags & GUI_THREAD_FLAG_EXIT) {
                service->threadFlags.clear(GUI_THREAD_FLAG_EXIT);
                break;
            }
        }
    }

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
            LOGGER.warn("nothing to draw");
        }

        // Unlock GUI and LVGL
        lvgl::unlock();
    } else {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
    }

    unlock();
}

bool GuiService::onStart(TT_UNUSED ServiceContext& service) {
    auto* screen_root = lv_screen_active();
    if (screen_root == nullptr) {
        LOGGER.error("No display found");
        return false;
    }

    thread = new Thread(
        "gui",
        4096, // Last known minimum was 2800 for launching desktop
        []() { return guiMain(); }
    );

    const auto loader = findLoaderService();
    assert(loader != nullptr);
    loader_pubsub_subscription = loader->getPubsub()->subscribe([this](auto event) {
        onLoaderEvent(event);
    });

    lvgl::lock(portMAX_DELAY);

    keyboardGroup = lv_group_create();
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

    statusbarWidget = lvgl::statusbar_create(vertical_container);

    auto* app_container = lv_obj_create(vertical_container);
    lv_obj_set_style_pad_all(app_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(app_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(app_container, LV_PCT(100));
    lv_obj_set_flex_grow(app_container, 1);
    lv_obj_set_flex_flow(app_container, LV_FLEX_FLOW_COLUMN);

    appRootWidget = app_container;

    lvgl::unlock();

    isStarted = true;

    thread->setPriority(THREAD_PRIORITY_SERVICE);
    thread->start();

    return true;
}

void GuiService::onStop(TT_UNUSED ServiceContext& service) {
    lock();

    const auto loader = findLoaderService();
    assert(loader != nullptr);
    loader->getPubsub()->unsubscribe(loader_pubsub_subscription);

    appToRender = nullptr;
    isStarted = false;

    auto task_handle = thread->getTaskHandle();
    threadFlags.set(GUI_THREAD_FLAG_EXIT);
    thread->join();
    delete thread;

    unlock();

    tt_check(lvgl::lock(1000 / portTICK_PERIOD_MS));
    lv_group_delete(keyboardGroup);
    lvgl::unlock();
}

void GuiService::requestDraw() {
    auto task_handle = thread->getTaskHandle();
    threadFlags.set(GUI_THREAD_FLAG_DRAW);
}

void GuiService::showApp(std::shared_ptr<app::AppInstance> app) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isStarted) {
        LOGGER.error("Failed to show app {}: GUI not started", app->getManifest().appId);
        return;
    }

    if (appToRender != nullptr && appToRender->getLaunchId() == app->getLaunchId()) {
        LOGGER.warn("Already showing {}", app->getManifest().appId);
        return;
    }

    LOGGER.info("Showing {}", app->getManifest().appId);
    // Ensure previous app triggers onHide() logic
    if (appToRender != nullptr) {
        hideApp();
    }

    appToRender = std::move(app);
    requestDraw();
}

void GuiService::hideApp() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isStarted) {
        LOGGER.error("Failed to hide app: GUI not started");
        return;
    }

    if (appToRender == nullptr) {
        LOGGER.warn("hideApp() called but no app is currently shown");
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
