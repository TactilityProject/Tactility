#include <Tactility/app/timezone/TimeZoneView.h>

#include <Tactility/StringUtils.h>

#include <location/location.h>

#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/spinner.h>

#include <generated_assets.h>

#include <string_view>

namespace tt::app::timezone {

constexpr auto* TAG = "TimeZoneView";

namespace {

// Bounds the list, so a short filter doesn't create hundreds of widgets
constexpr size_t MAX_ENTRIES = 50;
constexpr TickType_t FILTER_DELAY = pdMS_TO_TICKS(500);

/** Parses a line like "Europe/Amsterdam","CET-1CEST,M3.5.0,M10.5.0/3" */
bool parseEntry(const std::string& input, std::string& outName, std::string& outCode) {
    if (input.size() < 3) {
        return false;
    }
    std::string partial_strip = input.substr(1, input.size() - 3);
    auto first_end_quote = partial_strip.find('"');
    if (first_end_quote == std::string::npos) {
        return false;
    }
    outName = partial_strip.substr(0, first_end_quote);
    outCode = partial_strip.substr(first_end_quote + 3);
    return true;
}

/** Calls @a visitor for each time zone until it returns false */
void forEachTimeZone(const std::function<bool(const std::string& name, const std::string& code)>& visitor) {
    const std::string_view csv(reinterpret_cast<const char*>(asset_timezones_csv), asset_timezones_csv_size);
    std::string name;
    std::string code;
    size_t start = 0;
    while (start < csv.size()) {
        size_t end = csv.find('\n', start);
        end = end == std::string_view::npos ? csv.size() : end + 1;
        if (parseEntry(std::string(csv.substr(start, end - start)), name, code) && !visitor(name, code)) {
            break;
        }
        start = end;
    }
}

} // namespace

TimeZoneView::TimeZoneView(OnSelected onSelected) : onSelected(std::move(onSelected)) {
    filterTimer = std::make_unique<Timer>(Timer::Type::Once, FILTER_DELAY, [this] {
        loadEntries();
        lvgl_lock();
        rebuildList();
        lvgl_unlock();
    });
    loadEntries();
}

TimeZoneView::~TimeZoneView() {
    filterTimer->stop();
    cancelled = true;
    if (detectThread != nullptr) {
        detectThread->join();
    }
}

void TimeZoneView::createWidgets(lv_obj_t* parent) {
    // The search row, the message and the list share a card. Only the list scrolls, so the card keeps its size.
    auto* card = lvgl_card_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto* searchRow = lv_obj_create(card);
    lv_obj_remove_style_all(searchRow);
    lv_obj_set_size(searchRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(searchRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(searchRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(searchRow, LV_DPX(4), LV_STATE_DEFAULT);

    auto* searchIcon = lv_image_create(searchRow);
    lv_obj_set_style_image_recolor_opa(searchIcon, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(searchIcon, lv_theme_get_color_primary(parent), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(searchIcon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_image_set_src(searchIcon, LVGL_ICON_SHARED_SEARCH);

    textarea = lv_textarea_create(searchRow);
    lv_textarea_set_placeholder_text(textarea, "e.g. Europe/Amsterdam");
    lv_textarea_set_one_line(textarea, true);
    lv_obj_set_flex_grow(textarea, 1);

    // Detects the time zone from the location, with a spinner in its place while it does
    detectButton = lvgl_icon_button_create(searchRow);
    auto* detectLabel = lv_label_create(detectButton);
    lv_obj_set_style_text_font(detectLabel, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(detectLabel, LVGL_ICON_SHARED_LOCATION_ON);
    lv_obj_add_event_cb(detectButton, onDetectPressed, LV_EVENT_SHORT_CLICKED, this);
    detectSpinner = lvgl_spinner_create(searchRow);

    messageLabel = lv_label_create(card);
    lv_obj_set_width(messageLabel, LV_PCT(100));
    lv_label_set_long_mode(messageLabel, LV_LABEL_LONG_WRAP);

    list = lvgl_list_create(card);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);

    mutex.lock();
    lv_textarea_set_text(textarea, filter.c_str());
    mutex.unlock();
    // Added after restoring the text, so restoring it doesn't filter the list again
    lv_obj_add_event_cb(textarea, onFilterChanged, LV_EVENT_VALUE_CHANGED, this);

    showState();
    rebuildList();
}

void TimeZoneView::destroyWidgets() {
    textarea = nullptr;
    detectButton = nullptr;
    detectSpinner = nullptr;
    messageLabel = nullptr;
    list = nullptr;
}

void TimeZoneView::setSelectedName(const std::string& name) {
    mutex.lock();
    selectedName = name;
    mutex.unlock();
    if (list != nullptr) {
        rebuildList();
    }
}

void TimeZoneView::onFilterChanged(lv_event_t* event) {
    auto* view = static_cast<TimeZoneView*>(lv_event_get_user_data(event));
    view->mutex.lock();
    view->filter = string::lowercase(std::string(lv_textarea_get_text(view->textarea)));
    view->mutex.unlock();
    view->filterTimer->stop();
    view->filterTimer->start();
}

void TimeZoneView::onDetectPressed(lv_event_t* event) {
    auto* view = static_cast<TimeZoneView*>(lv_event_get_user_data(event));
    if (view->detecting.exchange(true)) {
        return;
    }
    view->mutex.lock();
    view->message.clear();
    view->mutex.unlock();
    view->showState();
    // The previous detection already ended, as detecting was false
    if (view->detectThread != nullptr) {
        view->detectThread->join();
    }
    view->detectThread = std::make_unique<Thread>("timezone_detect", 5120, [view] {
        view->detect();
        return 0;
    });
    view->detectThread->start();
}

void TimeZoneView::onEntryPressed(lv_event_t* event) {
    auto* view = static_cast<TimeZoneView*>(lv_event_get_user_data(event));
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    view->mutex.lock();
    if (index >= view->entries.size()) {
        view->mutex.unlock();
        return;
    }
    const Entry entry = view->entries[index];
    view->mutex.unlock();
    view->select(entry.name, entry.code);
}

void TimeZoneView::loadEntries() {
    mutex.lock();
    const std::string currentFilter = filter;
    mutex.unlock();

    std::vector<Entry> matches;
    forEachTimeZone([&matches, &currentFilter](const std::string& name, const std::string& code) {
        if (string::lowercase(name).find(currentFilter) != std::string::npos) {
            matches.push_back({ name, code });
        }
        return matches.size() < MAX_ENTRIES;
    });

    mutex.lock();
    entries = std::move(matches);
    mutex.unlock();
}

// Called with the LVGL lock held
void TimeZoneView::rebuildList() {
    if (list == nullptr) {
        return;
    }
    lv_obj_clean(list);
    mutex.lock();
    for (size_t index = 0; index < entries.size(); index++) {
        auto* button = lvgl_list_add_button(list, nullptr, entries[index].name.c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
        lv_obj_add_event_cb(button, onEntryPressed, LV_EVENT_SHORT_CLICKED, this);
        if (entries[index].name == selectedName) {
            lv_obj_add_state(button, LV_STATE_CHECKED);
        }
    }
    mutex.unlock();
}

// Called with the LVGL lock held
void TimeZoneView::showState() {
    if (detectButton == nullptr) {
        return;
    }
    const bool isDetecting = detecting;
    // Detection needs location services, which are loaded at boot
    lv_obj_set_hidden(detectButton, isDetecting || !location_is_enabled());
    lv_obj_set_hidden(detectSpinner, !isDetecting);
    mutex.lock();
    lv_label_set_text(messageLabel, message.c_str());
    lv_obj_set_hidden(messageLabel, message.empty());
    mutex.unlock();
}

// Runs on the detection thread, as the lookup blocks
void TimeZoneView::detect() {
    char name[LOCATION_TIME_ZONE_MAX_LENGTH + 1];
    const error_t error = location_lookup_time_zone(name, sizeof(name));
    std::string code;
    std::string failure;
    if (error == ERROR_NONE) {
        forEachTimeZone([&name, &code](const std::string& entryName, const std::string& entryCode) {
            if (entryName == name) {
                code = entryCode;
                return false;
            }
            return true;
        });
        if (code.empty()) {
            failure = std::string("The detected time zone ") + name + " isn't in the list.";
        }
    } else if (error == ERROR_INVALID_STATE) {
        failure = "Detecting the time zone needs location services. You can turn them on in Settings.";
    } else {
        failure = "The time zone couldn't be detected. Check the internet connection and try again.";
    }

    // Waits for the LVGL lock without blocking the destructor, which can be called with the lock held
    while (!lvgl_try_lock(pdMS_TO_TICKS(50))) {
        if (cancelled) {
            return;
        }
    }
    if (cancelled) {
        lvgl_unlock();
        return;
    }
    detecting = false;
    mutex.lock();
    message = failure;
    mutex.unlock();
    showState();
    if (failure.empty()) {
        LOG_I(TAG, "Detected %s", name);
        // Shows the detected time zone in the list
        if (textarea != nullptr) {
            lv_textarea_set_text(textarea, name);
        }
        select(name, code);
    }
    lvgl_unlock();
}

// Called with the LVGL lock held
void TimeZoneView::select(const std::string& name, const std::string& code) {
    setSelectedName(name);
    onSelected(name, code);
}

}
