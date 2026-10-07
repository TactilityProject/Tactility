#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/insets.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/icon_button.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <Tactility/app/AppGrid.h>
#include <Tactility/app/launcher/Favourites.h>
#include <Tactility/app/setup/Setup.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/Tactility.h>

namespace tt::app::launcher {

constexpr auto* TAG = "Launcher";

namespace {

struct IconEntry {
    const char* id;
    const char* icon;
};

constexpr IconEntry ICONS[] = {
    {"tactility.apphub",            LVGL_ICON_SHARED_DOWNLOAD},
    {"tactility.camera",            LVGL_ICON_SHARED_CAMERA},
    {"tactility.chat",              LVGL_ICON_SHARED_FORUM},
    {"tactility.files",             LVGL_ICON_SHARED_FOLDER},
    {"tactility.i2cscanner",        LVGL_ICON_SHARED_CABLE},
    {"tactility.notes",             LVGL_ICON_SHARED_EDIT_NOTE},
    {"tactility.screenshot",        LVGL_ICON_SHARED_IMAGE},
    {"tactility.settings",          LVGL_ICON_SHARED_SETTINGS},
    {"tactility.systeminfo",        LVGL_ICON_SHARED_DEVICES},
    {"tactility.terminal",          LVGL_ICON_SHARED_TERMINAL},
    {"tactility.webserversettings", LVGL_ICON_SHARED_CLOUD},
};

// Hidden apps that are still shown in the launcher
constexpr const char* SHOWN_HIDDEN_APP_IDS[] = {
    "tactility.files",
    "tactility.settings",
};

const char* appIcon(const ::AppManifest* manifest) {
    for (const auto& entry : ICONS) {
        if (!strcmp(manifest->id, entry.id)) return entry.icon;
    }
    return LVGL_ICON_SHARED_DEPLOYED_CODE;
}

bool isShown(const ::AppManifest& manifest) {
    if (manifest.category != APP_CATEGORY_USER && manifest.category != APP_CATEGORY_SYSTEM) {
        return false;
    }
    if ((manifest.flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
        return true;
    }
    return std::ranges::any_of(SHOWN_HIDDEN_APP_IDS, [&](const char* id) { return strcmp(manifest.id, id) == 0; });
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

// Apps from installed packages this device can't run (wrong device, too little RAM)
void collectIncompatibleAppIds(const AppPackage* package, void* context) {
    if (app_package_manifest_is_compatible(&package->package)) {
        return;
    }
    auto* ids = static_cast<std::vector<std::string>*>(context);
    for (size_t i = 0; i < package->app_id_count; i++) {
        ids->emplace_back(package->app_ids[i]);
    }
}

std::vector<AppGridItem> collectItems(void* userData);
void onAppClicked(const ::AppManifest& manifest, void* userData);
void onAppLongPressed(const ::AppManifest& manifest, void* userData);
void onAppKey(const ::AppManifest& manifest, uint32_t key, void* userData);

struct Context {
    Favourites favourites;
    AppGrid grid { AppGrid::Callbacks {
        .collect = collectItems,
        .onClicked = onAppClicked,
        .onLongPressed = onAppLongPressed,
        .onKey = onAppKey,
        .userData = this
    } };
};

std::vector<AppGridItem> collectItems(void* userData) {
    const auto* ctx = static_cast<Context*>(userData);
    const std::vector<std::string> favouriteIds = ctx->favourites.load();

    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::vector<std::string> incompatibleIds;
    app_manager_for_each_package(collectIncompatibleAppIds, &incompatibleIds);
    std::erase_if(collected, [&](const ::AppManifest& manifest) {
        return !isShown(manifest) || std::ranges::find(incompatibleIds, std::string(manifest.id)) != incompatibleIds.end();
    });

    std::vector<AppGridItem> items;
    items.reserve(collected.size());
    for (const auto& manifest : collected) {
        items.push_back({ manifest, appIcon(&manifest), Favourites::contains(favouriteIds, manifest.id) });
    }
    std::ranges::sort(items, [](const AppGridItem& a, const AppGridItem& b) {
        if (a.highlighted != b.highlighted) {
            return a.highlighted;
        }
        return strcmp(a.manifest.name, b.manifest.name) < 0;
    });
    return items;
}

void startApp(const char* appId) {
    uint32_t instance_id = 0;
    AppStartContext context;
    if (app_start_context_from_id(appId, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }
}

void onAppClicked(const ::AppManifest& manifest, void*) {
    startApp(manifest.id);
}

void toggleFavourite(Context* ctx, const char* appId, bool keepSelection) {
    if (!ctx->favourites.toggle(appId)) {
        LOG_E(TAG, "Failed to save favourites");
    }
    ctx->grid.requestRepopulate(keepSelection);
}

void onAppLongPressed(const ::AppManifest& manifest, void* userData) {
    toggleFavourite(static_cast<Context*>(userData), manifest.id, false);
}

void onAppKey(const ::AppManifest& manifest, uint32_t key, void* userData) {
    if (key == 'f' || key == 'F') {
        toggleFavourite(static_cast<Context*>(userData), manifest.id, true);
    }
}

void onShortcutPressed(lv_event_t* e) {
    startApp(static_cast<const char*>(lv_event_get_user_data(e)));
}

bool isAppRegistered(const char* appId) {
    ::AppManifest manifest;
    return app_manager_find_manifest(appId, &manifest) == ERROR_NONE;
}

bool supportsPowerOff() {
    bool supported = false;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &supported, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_power_off(device)) {
            *static_cast<bool*>(context) = true;
            return false; // stop iterating
        } else {
            return true; // continue iterating
        }
    });
    return supported;
}

void addShortcut(lv_obj_t* parent, const char* icon, const char* appId) {
    auto* button = lvgl_icon_button_create(parent);
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, icon);
    lv_obj_add_event_cb(button, onShortcutPressed, LV_EVENT_SHORT_CLICKED, const_cast<char*>(appId));
}

lv_obj_t* createBarSection(lv_obj_t* parent, lv_flex_align_t align) {
    auto* section = lv_obj_create(parent);
    lv_obj_set_size(section, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(section, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(section, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(section, align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    return section;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    // Power off on the left, the page number and page buttons on the right
    auto* bottom_bar = createBarSection(parent, LV_FLEX_ALIGN_START);
    lv_obj_set_width(bottom_bar, LV_PCT(100));
    lvgl_obj_add_edge_padding(bottom_bar);
    auto* left_section = createBarSection(bottom_bar, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(left_section, 1);
    auto* page_bar = createBarSection(bottom_bar, LV_FLEX_ALIGN_END);

    // The monochrome theme's primary color is black, also on a black background
    ctx->grid.setPrimaryColorIcons(!lvgl_theme_is_mono());
    ctx->grid.createWidgetsWithPageBar(parent, page_bar);
    lv_obj_move_foreground(bottom_bar);

    if (isAppRegistered("tactility.poweroff") && supportsPowerOff()) {
        addShortcut(left_section, LVGL_ICON_SHARED_POWER_SETTINGS_NEW, "tactility.poweroff");
    }
}

void runAutoStart() {
    settings::BootSettings boot_properties;
    AppStartContext context;
    if (
        // Auto-start due to built-in requirement
        strcmp(CONFIG_TT_AUTO_START_APP_ID, "") != 0 &&
        app_start_context_from_id(CONFIG_TT_AUTO_START_APP_ID, &context) == ERROR_NONE
    ) {
        LOG_I(TAG, "Starting %s", CONFIG_TT_AUTO_START_APP_ID);
        uint32_t app_launch_id;
        app_start_with_context(&context, &app_launch_id);
    } else if (
        // Auto-start due to user configuration
        settings::loadBootSettings(boot_properties) &&
        !boot_properties.autoStartAppId.empty() &&
        app_start_context_from_id(boot_properties.autoStartAppId.c_str(), &context) == ERROR_NONE
    ) {
        LOG_I(TAG, "Starting %s", boot_properties.autoStartAppId.c_str());
        uint32_t app_launch_id;
        app_start_with_context(&context, &app_launch_id);
    } else {
        // No auto-start, consider running system setup
        if (!setup::isCompleted()) {
            setup::start();
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    Context ctx;
    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    runAutoStart();

    // The launcher is meant to stay resident (it's the home screen) - it only gives up its
    // thread when app-module's scheduler asks it to (e.g. another new-model app is started).
    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        bool shouldClose = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
        if (shouldClose) break;
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.launcher",
    .name = "Launcher",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 5120, .desired_memory_capability = 0 }
};

// Kept for Tactility/Private/Tactility/app/launcher/Launcher.h's existing declaration (still
// used by the old, unconverted CrashDiagnostics app to return to the launcher after a crash).
uint32_t start() {
    uint32_t instance_id = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_with_context(&context, &instance_id);
    return instance_id;
}

} // namespace
