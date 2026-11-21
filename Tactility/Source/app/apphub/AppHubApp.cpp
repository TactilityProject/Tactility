#define L10N_APP "apphub"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/Mutex.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/TileGrid.h>
#include <Tactility/app/apphub/AppHubIcon.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <app_hub/app_hub.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/spinner.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <string_view>
#include <format>
#include <string>
#include <vector>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

namespace tt::app::apphub {

constexpr auto* TAG = "AppHub";

/** At this theme size (0 is compact, 100 is regular) or lower, the list doesn't have room for icons and descriptions */
constexpr uint8_t COMPACT_SIZE_MAX = 20;

bool isCompact() {
    return lvgl_theme_get_size() <= COMPACT_SIZE_MAX;
}

bool areIconsShown() {
    return areSvgIconsEnabled() && !isCompact();
}

extern const ::AppManifest manifest;

namespace {

enum class Filter {
    All,
    Installed,
    Updates
};

constexpr auto FILTER_LABELS = ::std::to_array<const char*>({ tr_noop("All"), tr_noop("Installed"), tr_noop("Updates") });

/** An icon that was loaded for an app. The image is null when the app has no icon. */
struct AppIcon {
    tt::std::string appId;
    /** The version of the app that the icon is of, as a new version has a new icon */
    uint64_t version;
    lv_draw_buf_t* image;
    bool recolor;
};

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* contentWrapper = nullptr;
    /** The list of apps, null when the content isn't a list */
    lv_obj_t* list = nullptr;
    lv_obj_t* refreshButton = nullptr;
    lv_obj_t* updateAllButton = nullptr;
    lv_obj_t* updateAllSpinner = nullptr;
    lv_obj_t* header = nullptr;
    /** Flexible space on both sides of the tabs, which centers them between the close button and the actions */
    lv_obj_t* spacerLeft = nullptr;
    lv_obj_t* spacerRight = nullptr;
    int32_t headerWidth = -1;
    /** The filter tabs, by Filter */
    lv_obj_t* chips[3] = {};
    lv_obj_t* errorCard = nullptr;
    lv_obj_t* errorLabel = nullptr;
    /** The refresh error that shows above the list, empty when there is none. Only used with the LVGL lock held. */
    tt::std::string errorMessage;
    lv_obj_t* searchRow = nullptr;
    lv_obj_t* searchTextarea = nullptr;
    /** Whether the search field shows, survives across a bury/resurface cycle. Only used with the LVGL lock held. */
    bool searchVisible = false;
    /** The search text, survives across a bury/resurface cycle. Only used with the LVGL lock held. */
    tt::std::string query;
    // Survives across a bury/resurface cycle
    Filter filter = Filter::All;
    /** The app ids of the list's buttons, by index */
    tt::std::vector<tt::std::string> appIds;
    /** Read from the catalog file on this app's own thread (see loadApps()), as the widget code can't do file operations */
    tt::std::vector<AppHubApp> apps;
    Mutex mutex;

    // Survives across a bury/resurface cycle (e.g. opening AppHubDetailsApp and returning),
    int32_t scrollY = 0;
    // Set by createWidgets(), consumed by the first showApps() after resurfacing. The refresh
    // itself runs async (see requestRefresh() below), so when showApps() first populates the
    // list, contentWrapper is still an empty spinner; scrollY must come from here instead of a
    // live read off it.
    bool restoreScrollOnNextShow = false;
    // Only the first createWidgets() call triggers a network refresh. The rest uses the cached catalog.
    bool needsInitialRefresh = true;

    TaskEventGroup* eventGroup = nullptr;
    uint32_t refreshRequestedBit = 0;
    ::std::atomic<bool> refreshRequested {false};
    /** Set when the window resurfaces: the installed apps can change without an App Hub event, e.g. through the Files app */
    ::std::atomic<bool> reloadRequested {false};

    /** Icons by app. Loaded on this app's own thread (see loadNextIcon()), guarded by the mutex. */
    tt::std::vector<AppIcon> icons;
    /** Images of icons that were replaced by a new version's icon. Rows can still show them, so they're freed when the app closes. */
    tt::std::vector<lv_draw_buf_t*> retiredIcons;
    /** Shows for apps without an icon, null when the icons aren't shown */
    lv_draw_buf_t* defaultIcon = nullptr;
    uint32_t iconRequestedBit = 0;
    ::std::atomic<bool> iconRequested {false};
    /** App events cancel icon downloads, so the app closes without waiting for one */
    AppHubInterrupt interrupt {};

    AppHubEventSubscription appHubSub {};
    /** The id of the queued catalog refresh, 0 when none is pending */
    ::std::atomic<uint32_t> refreshOperationId {0};
    /** The id of the running update of all apps, 0 when none is running */
    uint32_t updateAllOperationId = 0;
    /** The ids of the apps that failed to update during updateAllOperationId */
    tt::std::vector<tt::std::string> failedUpdates;
};


void showApps(Context* ctx);
void refresh(Context* ctx);

void requestIcons(Context* ctx) {
    ctx->iconRequested = true;
    task_event_group_signal(ctx->eventGroup, ctx->iconRequestedBit);
}

/** Requires the context's mutex */
AppIcon* findIconOfId(Context* ctx, const char* appId) {
    auto found = ::std::ranges::find_if(ctx->icons, [appId](const AppIcon& icon) { return icon.appId == appId; });
    return found == ctx->icons.end() ? nullptr : &*found;
}

/** The icon of the current version of the app */
AppIcon* findIcon(Context* ctx, const AppHubApp& app) {
    auto* icon = findIconOfId(ctx, app.id);
    return (icon != nullptr && icon->version == app.version_code) ? icon : nullptr;
}

/** Requires the context's mutex */
void storeIcon(Context* ctx, AppIcon icon) {
    auto* existing = findIconOfId(ctx, icon.appId.c_str());
    if (existing == nullptr) {
        ctx->icons.push_back(::std::move(icon));
        return;
    }
    if (existing->image != nullptr) {
        ctx->retiredIcons.push_back(existing->image);
    }
    *existing = ::std::move(icon);
}

uint32_t getIconSize() {
    return lvgl_get_shared_icon_large_font_height();
}

/**
 * Loads the icon of the first catalog app that has none yet and shows it. Runs on this app's own thread.
 * @return true when there can be more icons to load
 */
bool loadNextIcon(Context* ctx) {
    tt::std::string appId;
    uint64_t version = 0;
    bool hasIcon = true;
    ctx->mutex.lock();
    for (const auto& app : ctx->apps) {
        if (app.in_catalog && findIcon(ctx, app) == nullptr) {
            appId = app.id;
            version = app.version_code;
            hasIcon = app.has_icon;
            break;
        }
    }
    if (!appId.empty() && !hasIcon) {
        // Nothing to download: the default icon stays
        storeIcon(ctx, { appId, version, nullptr, false });
    }
    ctx->mutex.unlock();
    if (appId.empty()) {
        return false;
    }
    if (!hasIcon) {
        return true;
    }

    bool recolor = false;
    bool interrupted = false;
    auto* image = loadIcon(appId.c_str(), getIconSize(), &recolor, &ctx->interrupt, &interrupted);
    if (interrupted) {
        // Tried again when the app doesn't close
        return true;
    }

    lvgl_lock();
    ctx->mutex.lock();
    storeIcon(ctx, { appId, version, image, recolor });
    if (image != nullptr) {
        // Shows the icon in the app's entry, which has the icon widget as its first child
        const auto index = ::std::ranges::find(ctx->appIds, appId);
        if (index != ctx->appIds.end() && ctx->list != nullptr) {
            auto* row = lv_obj_get_child(ctx->list, static_cast<int32_t>(index - ctx->appIds.begin()));
            auto* first = (row != nullptr) ? lv_obj_get_child(row, 0) : nullptr;
            // The rows have no icon widget when they were built while the icons were hidden, e.g. in compact mode
            if (first != nullptr && lv_obj_has_class(first, &lv_image_class)) {
                setIcon(first, image, recolor);
            }
        }
    }
    ctx->mutex.unlock();
    lvgl_unlock();
    return true;
}

/** Forgets the apps that have no icon, so a refresh tries them again */
void forgetMissingIcons(Context* ctx) {
    ctx->mutex.lock();
    ::std::erase_if(ctx->icons, [](const AppIcon& icon) { return icon.image == nullptr; });
    ctx->mutex.unlock();
}

/** Reads the apps from App Hub for showApps(). Doesn't touch widgets, so it runs without the LVGL lock. */
void loadApps(Context* ctx) {
    // Counted first, so the vector is allocated once. Growing it by doubling needs several large blocks
    // while the old one is still alive, which aborts when the heap is fragmented, e.g. during a download.
    size_t count = 0;
    app_hub_search(nullptr, [](const AppHubApp*, void* context) {
        (*static_cast<size_t*>(context))++;
        return true;
    }, &count);
    tt::std::vector<AppHubApp> apps;
    apps.reserve(count);
    app_hub_search(nullptr, [](const AppHubApp* app, void* context) {
        static_cast<tt::std::vector<AppHubApp>*>(context)->push_back(*app);
        return true;
    }, &apps);
    ctx->mutex.lock();
    ctx->apps = ::std::move(apps);
    ctx->mutex.unlock();
    if (areIconsShown()) {
        requestIcons(ctx);
    }
}

bool containsIgnoringCase(const char* text, const char* part) {
    const ::std::string_view textView(text);
    const ::std::string_view partView(part);
    return ::std::ranges::search(textView, partView, [](char left, char right) {
        return ::std::tolower(static_cast<unsigned char>(left)) == ::std::tolower(static_cast<unsigned char>(right));
    }).begin() != textView.end() || partView.empty();
}

bool matchesQuery(const AppHubApp& app, const tt::std::string& query) {
    return containsIgnoringCase(app.name, query.c_str()) || containsIgnoringCase(app.short_description, query.c_str());
}

bool matchesFilter(const AppHubApp& app, Filter filter) {
    switch (filter) {
        case Filter::All:
            return true;
        case Filter::Installed:
            return app.installed;
        case Filter::Updates:
            return app.update_available;
    }
    return true;
}

const char* getEmptyFilterText(Filter filter) {
    return (filter == Filter::Updates) ? tr("No updates available") : tr("No installed apps");
}

void updateFilterChips(Context* ctx) {
    for (uint32_t i = 0; i < ::std::size(ctx->chips); i++) {
        lv_obj_set_state(ctx->chips[i], LV_STATE_CHECKED, i == static_cast<uint32_t>(ctx->filter));
    }
}

/** One line of text that ends in an ellipsis. The label needs a fixed height, as it would wrap otherwise. */
void setSingleLineEllipsis(lv_obj_t* label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

/**
 * The tabs wrap their content. When they don't fit, they share the width instead and their text ends in an ellipsis.
 */
void setChipsShared(Context* ctx, bool shared) {
    for (auto* chip : ctx->chips) {
        auto* label = lv_obj_get_child(chip, 0);
        lv_obj_set_flex_grow(chip, shared ? 1 : 0);
        lv_obj_set_width(chip, shared ? 0 : LV_SIZE_CONTENT);
        if (shared) {
            setSingleLineEllipsis(label);
        } else {
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        }
    }
}

/**
 * The spacers on both sides of the tabs take the space that is left, so the tabs are centered between the close button
 * and the actions. When the tabs don't fit, the spacers go and the tabs share the width. Requires the LVGL lock.
 */
void layoutHeader(Context* ctx) {
    if (ctx->header == nullptr) {
        return;
    }
    setChipsShared(ctx, false);
    for (auto* spacer : { ctx->spacerLeft, ctx->spacerRight }) {
        lv_obj_set_hidden(spacer, false);
    }
    lv_obj_update_layout(ctx->header);
    if (lv_obj_get_width(ctx->spacerLeft) <= 0) {
        for (auto* spacer : { ctx->spacerLeft, ctx->spacerRight }) {
            lv_obj_set_hidden(spacer, true);
        }
        setChipsShared(ctx, true);
        // Flex grow can't shrink the chips below their content, so they get an explicit width
        int32_t fixedWidth = 0;
        int32_t visibleCount = 0;
        for (uint32_t i = 0; i < lv_obj_get_child_count(ctx->header); i++) {
            auto* child = lv_obj_get_child(ctx->header, i);
            if (lv_obj_is_hidden(child)) {
                continue;
            }
            visibleCount++;
            if (::std::find(::std::begin(ctx->chips), ::std::end(ctx->chips), child) == ::std::end(ctx->chips)) {
                fixedWidth += lv_obj_get_width(child) + lv_obj_get_style_margin_left(child, LV_PART_MAIN) + lv_obj_get_style_margin_right(child, LV_PART_MAIN);
            }
        }
        const int32_t gaps = (visibleCount - 1) * lv_obj_get_style_pad_column(ctx->header, LV_PART_MAIN);
        // The margin of a chip, for its focus ring, takes room too
        const int32_t chipMargins = lv_obj_get_style_margin_left(ctx->chips[0], LV_PART_MAIN) + lv_obj_get_style_margin_right(ctx->chips[0], LV_PART_MAIN);
        const int32_t chipWidth = ::std::max<int32_t>(0, (lv_obj_get_content_width(ctx->header) - fixedWidth - gaps) / static_cast<int32_t>(::std::size(ctx->chips)) - chipMargins);
        for (auto* chip : ctx->chips) {
            lv_obj_set_flex_grow(chip, 0);
            lv_obj_set_width(chip, chipWidth);
        }
        lv_obj_update_layout(ctx->header);
    }
}

void onHeaderSizeChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Setting the widths changes the header's height, which would trigger this again
    const int32_t width = lv_obj_get_width(ctx->header);
    if (width != ctx->headerWidth) {
        ctx->headerWidth = width;
        layoutHeader(ctx);
    }
}

/**
 * Shows the spinner while App Hub is busy, and otherwise the update all button when there are updates.
 * Requires the LVGL lock.
 */
void updateToolbarActions(Context* ctx, bool hasUpdates) {
    if (ctx->updateAllSpinner == nullptr) {
        return;
    }
    const bool busy = app_hub_is_busy();
    lv_obj_set_hidden(ctx->updateAllSpinner, !busy);
    lv_obj_set_hidden(ctx->updateAllButton, busy || !hasUpdates);
    layoutHeader(ctx);
}

void onUpdateAllPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const error_t error = app_hub_update_all(nullptr);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue the update of all apps: %s", error_to_string(error));
        return;
    }
    // Shows the spinner
    showApps(ctx);
}

void updateUpdatesChipText(Context* ctx, size_t updateCount) {
    auto* chip = ctx->chips[static_cast<size_t>(Filter::Updates)];
    const auto text = i18n::format(updateCount == 1 ? tr("{} update") : tr("{} updates"), { ::std::to_string(updateCount) });
    lv_label_set_text(lv_obj_get_child(chip, 0), text.c_str());
}

void onQueryChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->query = lv_textarea_get_text(lv_event_get_target_obj(event));
    // A different query shows a different list, which starts at the top
    ctx->scrollY = 0;
    ctx->restoreScrollOnNextShow = true;
    showApps(ctx);
}

/** Shows or hides the search field. Hiding it also clears the search. */
void onSearchTogglePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->searchVisible = !ctx->searchVisible;
    lv_obj_set_hidden(ctx->searchRow, !ctx->searchVisible);
    if (ctx->searchVisible) {
        lv_group_focus_obj(ctx->searchTextarea);
        return;
    }
    if (!ctx->query.empty()) {
        ctx->query.clear();
        lv_textarea_set_text(ctx->searchTextarea, "");
        ctx->scrollY = 0;
        ctx->restoreScrollOnNextShow = true;
        showApps(ctx);
    }
}

/** A search field with an icon, above the list */
lv_obj_t* createSearchRow(lv_obj_t* parent, Context* ctx) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(row, LV_DPX(8), LV_STATE_DEFAULT);

    auto* icon = lv_image_create(row);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(icon, lv_theme_get_color_primary(parent), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_image_set_src(icon, LVGL_ICON_SHARED_SEARCH);

    ctx->searchRow = row;
    lv_obj_set_hidden(row, !ctx->searchVisible);
    ctx->searchTextarea = lv_textarea_create(row);
    lv_textarea_set_placeholder_text(ctx->searchTextarea, tr("Search apps"));
    lv_textarea_set_one_line(ctx->searchTextarea, true);
    lv_obj_set_flex_grow(ctx->searchTextarea, 1);
    lv_textarea_set_text(ctx->searchTextarea, ctx->query.c_str());
    // Added after restoring the text, so restoring it doesn't filter the list again
    lv_obj_add_event_cb(ctx->searchTextarea, onQueryChanged, LV_EVENT_VALUE_CHANGED, ctx);
    return row;
}

void onFilterPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_target_obj(event);
    const auto filter = static_cast<Filter>(::std::find(::std::begin(ctx->chips), ::std::end(ctx->chips), chip) - ::std::begin(ctx->chips));
    if (filter == ctx->filter) {
        return;
    }
    ctx->filter = filter;
    updateFilterChips(ctx);
    // A different filter shows a different list, which starts at the top
    ctx->scrollY = 0;
    ctx->restoreScrollOnNextShow = true;
    showApps(ctx);
}

/** An icon button for the header, with a symbol of the default font or of the shared icon font */
lv_obj_t* createHeaderButton(lv_obj_t* parent, const char* symbol, bool sharedIconFont, lv_event_cb_t callback, Context* ctx) {
    auto* button = lvgl_icon_button_create(parent);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, ctx);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    if (sharedIconFont) {
        lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    }
    return button;
}

/** An invisible flexible space between the header's widgets. It's not clickable, so grid navigation skips it. */
lv_obj_t* createHeaderSpacer(lv_obj_t* parent) {
    auto* spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 0, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_clickable(spacer, false);
    return spacer;
}

/** Creates the filter tabs as children of the header. They wrap their text, see layoutHeader() for when they don't fit. */
void createFilterChips(lv_obj_t* parent, Context* ctx) {
    for (size_t i = 0; i < ::std::size(FILTER_LABELS); i++) {
        auto* chip = lvgl_chip_create(parent);
        ctx->chips[i] = chip;
        lv_obj_add_event_cb(chip, onFilterPressed, LV_EVENT_SHORT_CLICKED, ctx);
        auto* chipLabel = lv_label_create(chip);
        lv_label_set_text(chipLabel, tr(FILTER_LABELS[i]));
        lv_obj_set_style_text_align(chipLabel, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    }
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onAppPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    auto* widget = lv_event_get_target_obj(e);
    const auto* user_data = lv_obj_get_user_data(widget);
    const intptr_t index = reinterpret_cast<intptr_t>(user_data);
    ctx->mutex.lock();
    if (index < ctx->appIds.size()) {
        apphubdetails::start(ctx->appIds[index].c_str());
    }
    ctx->mutex.unlock();
}

void requestRefresh(Context* ctx) {
    ctx->refreshRequested = true;
    task_event_group_signal(ctx->eventGroup, ctx->refreshRequestedBit);
}

void requestReload(Context* ctx) {
    ctx->reloadRequested = true;
    task_event_group_signal(ctx->eventGroup, ctx->refreshRequestedBit);
}

void onRefreshPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    requestRefresh(ctx);
}

lv_obj_t* createCard(lv_obj_t* parent) {
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    return card;
}

/** Replaces the content with a centered card that holds the status text */
void showStatusLabel(Context* ctx, const char* text) {
    lv_obj_clean(ctx->contentWrapper);

    auto* card = createCard(ctx->contentWrapper);
    lv_obj_set_style_text_align(card, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    auto* label = lv_label_create(card);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(label, text);
}

/** Shows the error between the search field and the list, or hides it for nullptr. Requires the LVGL lock. */
void setError(Context* ctx, const char* message) {
    ctx->errorMessage = (message != nullptr) ? message : "";
    if (ctx->errorCard == nullptr) {
        return;
    }
    lv_label_set_text(ctx->errorLabel, ctx->errorMessage.c_str());
    lv_obj_set_hidden(ctx->errorCard, ctx->errorMessage.empty());
    lv_obj_set_hidden(ctx->refreshButton, ctx->errorMessage.empty());
    layoutHeader(ctx);
}

void showApps(Context* ctx);

/** The apps that were already loaded stay in the list */
void showRefreshFailedError(Context* ctx, const char* message) {
    // The message is kept for when the window is shown again. The list is only rebuilt while it exists.
    setError(ctx, message);
    if (ctx->contentWrapper != nullptr) {
        showApps(ctx);
    }
}

void showNoInternet(Context* ctx) {
    showRefreshFailedError(ctx, tr("No Internet Connection"));
}

bool isInContentWrapper(Context* ctx, lv_obj_t* obj) {
    for (auto* parent = lv_obj_get_parent(obj); parent != nullptr; parent = lv_obj_get_parent(parent)) {
        if (parent == ctx->contentWrapper) {
            return true;
        }
    }
    return false;
}

void showApps(Context* ctx) {
    // Refresh rebuilds the list from scratch (cached copy, then again once the network fetch
    // lands), which would otherwise reset the user's scroll position each time.
    int32_t scrollY;
    if (ctx->restoreScrollOnNextShow) {
        scrollY = ctx->scrollY;
        ctx->restoreScrollOnNextShow = false;
    } else {
        scrollY = lv_obj_get_scroll_y(ctx->contentWrapper);
    }
    ctx->mutex.lock();
    // Rebuilding deletes the focused entry button, so the same app is focused again afterwards
    tt::std::string focused_app_id;
    bool focused_by_key = false;
    auto* group = lv_group_get_default();
    auto* focused = group != nullptr ? lv_group_get_focused(group) : nullptr;
    if (focused != nullptr && isInContentWrapper(ctx, focused)) {
        const auto index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(focused));
        if (index < ctx->appIds.size()) {
            focused_app_id = ctx->appIds[index];
            focused_by_key = lv_obj_has_state(focused, LV_STATE_FOCUS_KEY);
        }
    }
    lv_obj_clean(ctx->contentWrapper);
    ctx->list = nullptr;

    const bool iconsEnabled = areIconsShown();
    const bool showDescriptions = !isCompact();
    const uint32_t iconSize = getIconSize();
    tt::std::vector<AppHubApp> apps = ctx->apps;
    const bool hasApps = !apps.empty();
    updateUpdatesChipText(ctx, ::std::ranges::count_if(apps, [](const AppHubApp& app) { return app.update_available; }));
    layoutHeader(ctx);
    updateToolbarActions(ctx, ::std::ranges::any_of(apps, [](const AppHubApp& app) { return app.update_available; }));
    ::std::erase_if(apps, [ctx](const AppHubApp& app) {
        return !matchesFilter(app, ctx->filter) || !matchesQuery(app, ctx->query);
    });
    ctx->appIds.clear();

    if (!apps.empty()) {
        auto* card = createCard(ctx->contentWrapper);
        lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
        // The list items' pressed and focused backgrounds follow the card's rounded corners
        lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);

        // The card provides the background
        auto* list = lvgl_list_create(card);
        ctx->list = list;
        lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
        lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);
        lv_obj_t* focus_target = nullptr;
        for (int i = 0; i < apps.size(); i++) {
            const auto& app = apps[i];
            auto* entry_button = lvgl_list_add_button(list, nullptr, nullptr);
            lv_obj_set_flex_align(entry_button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            if (iconsEnabled) {
                // The first child, so the icon can be set later when it's loaded. The app icon glyph shows until then.
                auto* image = lv_image_create(entry_button);
                lv_obj_set_size(image, iconSize, iconSize);
                const auto* appIcon = findIcon(ctx, app);
                if (appIcon != nullptr && appIcon->image != nullptr) {
                    setIcon(image, appIcon->image, appIcon->recolor);
                } else if (ctx->defaultIcon != nullptr) {
                    setIcon(image, ctx->defaultIcon, true);
                }
            }

            auto* texts = lv_obj_create(entry_button);
            // Without the theme's styles, the text color of the entry applies, which follows its focused and pressed states
            lv_obj_remove_style_all(texts);
            lv_obj_set_height(texts, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(texts, 1);
            lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_clickable(texts, false);
            lv_obj_set_scrollable(texts, false);

            auto* name_label = lv_label_create(texts);
            lv_label_set_text(name_label, app.name);
            setSingleLineEllipsis(name_label);

            if (showDescriptions) {
                auto* description_label = lv_label_create(texts);
                lv_label_set_text(description_label, app.short_description);
                lv_obj_set_style_text_font(description_label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
                setSingleLineEllipsis(description_label);
            }

            if (app.operation_pending) {
                auto* status = lv_label_create(entry_button);
                lv_label_set_text(status, LVGL_ICON_SHARED_HOURGLASS);
                lv_obj_set_style_text_font(status, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
                if (!lvgl_theme_is_mono()) {
                    lv_obj_set_style_text_color(status, lv_theme_get_color_secondary(status), LV_STATE_DEFAULT);
                }
            } else if (app.installed) {
                auto* status = lv_label_create(entry_button);
                lv_label_set_text(status, app.update_available ? tr("Update available") : tr("Installed"));
                lv_obj_set_style_text_font(status, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
                if (!lvgl_theme_is_mono()) {
                    lv_obj_set_style_text_color(status, lv_theme_get_color_secondary(status), LV_STATE_DEFAULT);
                }
            }
            auto int_as_voidptr = reinterpret_cast<void*>(i);
            lv_obj_set_user_data(entry_button, int_as_voidptr);
            lv_obj_add_event_cb(entry_button, onAppPressed, LV_EVENT_SHORT_CLICKED, ctx);
            ctx->appIds.emplace_back(app.id);
            if (app.id == focused_app_id) {
                focus_target = entry_button;
            }
        }

        lvgl_list_hide_last_divider(list);
        lv_obj_scroll_to_y(ctx->contentWrapper, scrollY, LV_ANIM_OFF);
        if (focus_target != nullptr) {
            lv_group_focus_obj(focus_target);
            if (focused_by_key) {
                lv_obj_add_state(focus_target, LV_STATE_FOCUS_KEY);
            }
        }
    } else if (hasApps) {
        showStatusLabel(ctx, ctx->query.empty() ? getEmptyFilterText(ctx->filter) : tr("No apps found"));
    } else if (ctx->refreshOperationId == 0) {
        // A more specific error stays
        if (ctx->errorMessage.empty()) {
            setError(ctx, tr("Failed to load content"));
        }
    } else {
        showStatusLabel(ctx, tr("Loading..."));
    }
    ctx->mutex.unlock();
}

// Runs on appMain()'s own task (triggered via requestRefresh()), never directly from the LVGL task.
void refresh(Context* ctx) {
    // Buried (e.g. AppHubDetailsApp is open): destroyWidgets() already released these. A refresh
    // request queued just before burying could still land here, so re-check rather than assume
    // requestRefresh() and refresh() always run against a live window.
    if (ctx->refreshOperationId != 0) {
        return;
    }

    lvgl_lock();
    if (ctx->contentWrapper == nullptr) {
        lvgl_unlock();
        return;
    }
    setError(ctx, nullptr);
    lvgl_unlock();

    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    Device* wifi_device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) == ERROR_NONE) {
        wifi_get_station_state(wifi_device, &station_state);
        device_put(wifi_device);
    }

    if (station_state != WIFI_STATION_STATE_CONNECTED) {
        lvgl_lock();
        showNoInternet(ctx);
        lvgl_unlock();
        return;
    }

    uint32_t operation_id = 0;
    if (app_hub_refresh(&operation_id) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue the catalog refresh");
        lvgl_lock();
        showRefreshFailedError(ctx, tr("Cannot reach server"));
        lvgl_unlock();
        return;
    }
    ctx->refreshOperationId = operation_id;
    forgetMissingIcons(ctx);

    // Shows the cached catalog while the new one downloads
    loadApps(ctx);
    lvgl_lock();
    if (ctx->contentWrapper != nullptr) {
        showApps(ctx);
    }
    lvgl_unlock();
}

// Runs on appMain()'s own task, after the window resurfaced
void reload(Context* ctx) {
    loadApps(ctx);
    lvgl_lock();
    if (ctx->contentWrapper != nullptr) {
        showApps(ctx);
    }
    lvgl_unlock();
}

void onOperationEvent(Context* ctx, const AppHubEvent& event) {
    if (event.type == APP_HUB_EVENT_REFRESHING) {
        // The list doesn't change yet, only the toolbar shows that App Hub is busy
        lvgl_lock();
        if (ctx->updateAllButton != nullptr) {
            updateToolbarActions(ctx, !lv_obj_is_hidden(ctx->updateAllButton));
        }
        lvgl_unlock();
        return;
    }

    const bool isRefresh = event.operation_id == ctx->refreshOperationId;
    if (isRefresh) {
        ctx->refreshOperationId = 0;
        ctx->needsInitialRefresh = event.result != ERROR_NONE;
        if (event.result != ERROR_NONE) {
            LOG_E(TAG, "Catalog refresh failed: %s", error_to_string(event.result));
        }
    }

    loadApps(ctx);
    lvgl_lock();
    if (ctx->contentWrapper == nullptr) {
        // Buried (e.g. AppHubDetailsApp or a dialog is open): createWidgets() rebuilds the list on resurface
    } else if (isRefresh && event.result != ERROR_NONE) {
        showRefreshFailedError(ctx, tr("Cannot reach server"));
    } else {
        if (isRefresh) {
            setError(ctx, nullptr);
        }
        showApps(ctx);
    }
    lvgl_unlock();
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    // Replaces the toolbar: the close button, the tabs in the middle and the actions. All focusable widgets are
    // direct children of the header, so they're one grid navigation group.
    ctx->header = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->header);
    lv_obj_set_size(ctx->header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const auto spacing = lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing());
    lv_obj_set_style_pad_hor(ctx->header, spacing, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ctx->header, spacing, LV_STATE_DEFAULT);
    ctx->headerWidth = -1;
    lv_obj_add_event_cb(ctx->header, onHeaderSizeChanged, LV_EVENT_SIZE_CHANGED, ctx);

    createHeaderButton(ctx->header, LVGL_ICON_SHARED_CLOSE, true, onBackPressed, ctx);
    ctx->spacerLeft = createHeaderSpacer(ctx->header);
    createFilterChips(ctx->header, ctx);
    updateFilterChips(ctx);
    ctx->spacerRight = createHeaderSpacer(ctx->header);
    ctx->refreshButton = createHeaderButton(ctx->header, LVGL_ICON_SHARED_REFRESH, true, onRefreshPressed, ctx);
    lv_obj_set_hidden(ctx->refreshButton, true);
    createHeaderButton(ctx->header, LVGL_ICON_SHARED_SEARCH, true, onSearchTogglePressed, ctx);
    ctx->updateAllButton = createHeaderButton(ctx->header, LVGL_ICON_SHARED_DOWNLOAD, true, onUpdateAllPressed, ctx);
    lv_obj_set_hidden(ctx->updateAllButton, true);
    ctx->updateAllSpinner = lvgl_spinner_create(ctx->header);
    lv_obj_set_clickable(ctx->updateAllSpinner, false);
    lv_obj_set_hidden(ctx->updateAllSpinner, true);
    lvgl_grid_navigation_add(ctx->header);

    createSearchRow(parent, ctx);

    LvglThemeSettings theme {};
    lvgl_theme_get_settings(&theme);
    // The wrapper centers the card, which is only as wide as its text
    auto* errorWrapper = lv_obj_create(parent);
    lv_obj_remove_style_all(errorWrapper);
    lv_obj_set_size(errorWrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(errorWrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(errorWrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ctx->errorCard = createCard(errorWrapper);
    lv_obj_set_width(ctx->errorCard, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(ctx->errorCard, LV_PCT(100), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ctx->errorCard, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ctx->errorCard, theme.color_error, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->errorCard, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ctx->errorCard, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ctx->errorCard, lvgl_theme_lerp(LV_DPX(1), LV_DPX(4), lvgl_theme_get_spacing()), LV_STATE_DEFAULT);
    // Space above the card scales with the theme's spacing. A hidden card takes no space.
    lv_obj_set_style_margin_top(ctx->errorCard, lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing()), LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ctx->errorCard, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    ctx->errorLabel = lv_label_create(ctx->errorCard);
    lv_label_set_long_mode(ctx->errorLabel, LV_LABEL_LONG_MODE_WRAP);
    setError(ctx, ctx->errorMessage.c_str());

    ctx->contentWrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(ctx->contentWrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(ctx->contentWrapper, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->contentWrapper, 1);
    // The header gets the same space above as the content has below it, which scales with the theme's spacing
    lv_obj_set_style_pad_top(ctx->header, lv_obj_get_style_pad_top(ctx->contentWrapper, LV_PART_MAIN), LV_STATE_DEFAULT);

    ctx->restoreScrollOnNextShow = true;
    if (ctx->needsInitialRefresh && ctx->refreshOperationId == 0) {
        requestRefresh(ctx);
    } else {
        // Resurfacing (e.g. returning from AppHubDetailsApp): redisplay the cached catalog
        // instead of hitting the network again. window_manager calls createWidgets() with the
        // LVGL lock already held, so this can touch widgets directly.
        showApps(ctx);
        // The cache can be stale, so the app's task reloads it and shows the list again
        requestReload(ctx);
    }
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->scrollY = lv_obj_get_scroll_y(ctx->contentWrapper);
    ctx->contentWrapper = nullptr;
    ctx->list = nullptr;
    ctx->refreshButton = nullptr;
    ctx->updateAllButton = nullptr;
    ctx->updateAllSpinner = nullptr;
    ::std::fill(::std::begin(ctx->chips), ::std::end(ctx->chips), nullptr);
    ctx->header = nullptr;
    ctx->spacerLeft = nullptr;
    ctx->spacerRight = nullptr;
    ctx->searchRow = nullptr;
    ctx->searchTextarea = nullptr;
    ctx->errorCard = nullptr;
    ctx->errorLabel = nullptr;
}

/** Tracks the update of all apps, and shows which apps failed when it's done */
void onUpdateAllEvent(Context* ctx, const AppHubEvent& event) {
    if (event.type == APP_HUB_EVENT_UPDATING) {
        ctx->updateAllOperationId = event.operation_id;
        ctx->failedUpdates.clear();
    } else if (event.type == APP_HUB_EVENT_INSTALLED && event.operation_id == ctx->updateAllOperationId && event.result != ERROR_NONE) {
        ctx->failedUpdates.emplace_back(event.app_id);
    } else if (event.type == APP_HUB_EVENT_UPDATED && event.operation_id == ctx->updateAllOperationId) {
        ctx->updateAllOperationId = 0;
        if (!ctx->failedUpdates.empty()) {
            tt::std::string names;
            for (const auto& id : ctx->failedUpdates) {
                AppHubApp app {};
                if (!names.empty()) {
                    names += ", ";
                }
                names += (app_hub_get_app(id.c_str(), &app) == ERROR_NONE) ? app.name : id.c_str();
            }
            const auto count = ctx->failedUpdates.size();
            alertdialog::start(ctx->appInstanceId, tr("Update failed"), i18n::format(count == 1 ? tr("{} app failed to update: {}") : tr("{} apps failed to update: {}"), { ::std::to_string(count), names }));
            ctx->failedUpdates.clear();
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx;
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.iconRequestedBit) == ERROR_NONE);
    if (task_event_group_claim_bit(&event_group, &ctx.refreshRequestedBit) != ERROR_NONE) {
        LOG_W(TAG, "Failed to claim a refresh-requested bit; refresh button won't work");
    }

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);
    check(app_hub_event_subscribe(&ctx.appHubSub, &event_group) == ERROR_NONE);
    ctx.interrupt = { &event_group, sub.bit };

    if (areIconsShown()) {
        ctx.defaultIcon = loadDefaultIcon(getIconSize());
    }
    loadApps(&ctx);
    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
        }

        AppHubEvent app_hub_event {};
        while (app_hub_event_poll(&ctx.appHubSub, &app_hub_event) == ERROR_NONE) {
            onUpdateAllEvent(&ctx, app_hub_event);
            onOperationEvent(&ctx, app_hub_event);
        }

        if (!shouldClose && ctx.reloadRequested.exchange(false)) {
            reload(&ctx);
        }
        if (!shouldClose && ctx.refreshRequested.exchange(false)) {
            refresh(&ctx);
        }
        // One icon per iteration, so closing the app and operations stay responsive while icons download
        if (!shouldClose && ctx.iconRequested.exchange(false) && loadNextIcon(&ctx)) {
            requestIcons(&ctx);
        }
    }

    window_manager_remove(window);
    if (ctx.defaultIcon != nullptr) {
        lv_draw_buf_destroy(ctx.defaultIcon);
    }
    for (auto* image : ctx.retiredIcons) {
        lv_draw_buf_destroy(image);
    }
    for (auto& icon : ctx.icons) {
        if (icon.image != nullptr) {
            lv_draw_buf_destroy(icon.image);
        }
    }
    check(app_hub_event_unsubscribe(&ctx.appHubSub) == ERROR_NONE);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.apphub",
    .name = "App Hub",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {
        .depth = 10240,
        .desired_memory_capability = 0,
    },
};

} // namespace tt::app::apphub
