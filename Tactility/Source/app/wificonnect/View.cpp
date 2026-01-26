#include <Tactility/TactilityCore.h>

#include <Tactility/app/wificonnect/View.h>
#include <Tactility/app/wificonnect/WifiConnect.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/Spinner.h>
#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/service/wifi/WifiGlobals.h>

#include <lvgl.h>
#include <cstring>

namespace tt::app::wificonnect {

static const auto LOGGER = Logger("WifiConnect");

void View::resetErrors() {
    lv_obj_add_flag(password_error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ssid_error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(connection_error, LV_OBJ_FLAG_HIDDEN);
}

static void onConnect(lv_event_t* event) {
    auto wifi = std::static_pointer_cast<WifiConnect>(getCurrentApp());
    auto& view = wifi->getView();

    wifi->getState().setConnectionError(false);
    view.resetErrors();

    const char* ssid = lv_textarea_get_text(view.ssid_textarea);
    size_t ssid_len = strlen(ssid);
    if (ssid_len > TT_WIFI_SSID_LIMIT) {
        LOGGER.error("SSID too long");
        lv_label_set_text(view.ssid_error, "SSID too long");
        lv_obj_remove_flag(view.ssid_error, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const char* password = lv_textarea_get_text(view.password_textarea);
    size_t password_len = strlen(password);
    if (password_len > TT_WIFI_CREDENTIALS_PASSWORD_LIMIT) {
        LOGGER.error("Password too long");
        lv_label_set_text(view.password_error, "Password too long");
        lv_obj_remove_flag(view.password_error, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    bool store = lv_obj_get_state(view.remember_switch) & LV_STATE_CHECKED;

    view.setLoading(true);

    service::wifi::settings::WifiApSettings settings;
    settings.password = password;
    settings.ssid = ssid;
    settings.channel = 0;
    settings.autoConnect = TT_WIFI_AUTO_CONNECT; // No UI yet, so use global setting:w

    auto* bindings = &wifi->getBindings();
    bindings->onConnectSsid(
        settings,
        store,
        bindings->onConnectSsidContext
    );
}

void View::setLoading(bool loading) {
    if (loading) {
        lv_obj_add_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(connecting_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(password_textarea, LV_STATE_DISABLED);
        lv_obj_add_state(ssid_textarea, LV_STATE_DISABLED);
        lv_obj_add_state(remember_switch, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_flag(connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connecting_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(password_textarea, LV_STATE_DISABLED);
        lv_obj_remove_state(ssid_textarea, LV_STATE_DISABLED);
        lv_obj_remove_state(remember_switch, LV_STATE_DISABLED);
    }
}

void View::createBottomButtons(lv_obj_t* parent) {
    auto* button_container = lv_obj_create(parent);
    lv_obj_set_width(button_container, LV_PCT(100));
    lv_obj_set_height(button_container, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(button_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(button_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button_container, 0, LV_STATE_DEFAULT);

    remember_switch = lv_switch_create(button_container);
    lv_obj_add_state(remember_switch, LV_STATE_CHECKED);
    lv_obj_align(remember_switch, LV_ALIGN_LEFT_MID, 0, 0);

    auto* remember_label = lv_label_create(button_container);
    lv_label_set_text(remember_label, "Remember");
    lv_obj_align(remember_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align_to(remember_label, remember_switch, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    connecting_spinner = tt::lvgl::spinner_create(button_container);
    lv_obj_align(connecting_spinner, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(connecting_spinner, LV_OBJ_FLAG_HIDDEN);

    connect_button = lv_btn_create(button_container);
    auto* connect_label = lv_label_create(connect_button);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_align(connect_button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(connect_button, &onConnect, LV_EVENT_SHORT_CLICKED, nullptr);
}

// TODO: Standardize dialogs
void View::init(AppContext& app, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lvgl::toolbar_create(parent, app);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    // SSID

    auto* ssid_wrapper = lv_obj_create(wrapper);
    lv_obj_set_width(ssid_wrapper, LV_PCT(100));
    lv_obj_set_height(ssid_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ssid_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(ssid_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ssid_wrapper, 0, LV_STATE_DEFAULT);

    auto* ssid_label_wrapper = lv_obj_create(ssid_wrapper);
    lv_obj_set_width(ssid_label_wrapper, LV_PCT(50));
    lv_obj_set_height(ssid_label_wrapper, LV_SIZE_CONTENT);
    lv_obj_align(ssid_label_wrapper, LV_ALIGN_LEFT_MID, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ssid_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ssid_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ssid_label_wrapper, 0, LV_STATE_DEFAULT);

    auto* ssid_label = lv_label_create(ssid_label_wrapper);
    lv_label_set_text(ssid_label, "Network:");

    ssid_textarea = lv_textarea_create(ssid_wrapper);
    lv_textarea_set_one_line(ssid_textarea, true);
    lv_obj_align(ssid_textarea, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(ssid_textarea, LV_PCT(50));

    ssid_error = lv_label_create(wrapper);
    lv_obj_set_style_text_color(ssid_error, lv_color_make(255, 50, 50), LV_STATE_DEFAULT);
    lv_obj_add_flag(ssid_error, LV_OBJ_FLAG_HIDDEN);

    // Password

    auto* password_wrapper = lv_obj_create(wrapper);
    lv_obj_set_width(password_wrapper, LV_PCT(100));
    lv_obj_set_height(password_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(password_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(password_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(password_wrapper, 0, LV_STATE_DEFAULT);

    auto* password_label_wrapper = lv_obj_create(password_wrapper);
    lv_obj_set_width(password_label_wrapper, LV_PCT(50));
    lv_obj_set_height(password_label_wrapper, LV_SIZE_CONTENT);
    lv_obj_align_to(password_label_wrapper, password_wrapper, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(password_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(password_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(password_label_wrapper, 0, LV_STATE_DEFAULT);

    auto* password_label = lv_label_create(password_label_wrapper);
    lv_label_set_text(password_label, "Password:");

    password_textarea = lv_textarea_create(password_wrapper);
    lv_textarea_set_one_line(password_textarea, true);
    lv_textarea_set_password_mode(password_textarea, true);
    lv_obj_align(password_textarea, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(password_textarea, LV_PCT(50));

    password_error = lv_label_create(wrapper);
    lv_obj_set_style_text_color(password_error, lv_color_make(255, 50, 50), LV_STATE_DEFAULT);
    lv_obj_add_flag(password_error, LV_OBJ_FLAG_HIDDEN);

    // Connection error
    connection_error = lv_label_create(wrapper);
    lv_obj_set_style_text_color(connection_error, lv_color_make(255, 50, 50), LV_STATE_DEFAULT);
    lv_obj_add_flag(connection_error, LV_OBJ_FLAG_HIDDEN);

    // Bottom buttons
    createBottomButtons(wrapper);

    // Init from app parameters
    auto bundle = app.getParameters();
    if (bundle != nullptr) {
        std::string ssid;
        if (optSsidParameter(bundle, ssid)) {
            lv_textarea_set_text(ssid_textarea, ssid.c_str());

            if (!ssid.empty()) {
                lv_group_focus_obj(password_textarea);
            }
        }

        std::string password;
        if (optPasswordParameter(bundle, password)) {
            lv_textarea_set_text(password_textarea, password.c_str());
        }
    }
}

void View::update() {
    if (state->hasConnectionError()) {
        setLoading(false);
        resetErrors();
        lv_label_set_text(connection_error, "Connection failed");
        lv_obj_remove_flag(connection_error, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace
