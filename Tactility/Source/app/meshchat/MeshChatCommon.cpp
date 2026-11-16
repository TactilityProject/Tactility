#define L10N_APP "mesh"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>

#include <app/start.h>

#include <lvgl/widgets/card.h>

#include <tactility/log.h>

namespace tt::app::meshchat {

constexpr auto* TAG = "MeshChat";

/** The padding of the content area that holds the cards */
#define CONTENT_PADDING LV_DPX(4)

void startApp(const char* id, int argc, const char* const argv[]) {
    AppStartContext context;
    if (app_start_context_from_id(id, &context) != ERROR_NONE) {
        LOG_E(TAG, "Failed to find %s", id);
        return;
    }
    app_start_context_set_arguments_ext(&context, argc, argv);
    AppInstanceId instanceId = 0;
    if (app_start_with_context(&context, &instanceId) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start %s", id);
    }
}

static const char* getNetworkLabel(const char* id) {
    if (strcmp(id, "meshcore") == 0) return "MeshCore";
    if (strcmp(id, "meshtastic") == 0) return "Meshtastic";
    return id;
}

NetworkInfo loadNetworkInfo() {
    NetworkInfo info;
    // A backend and a radio stay selected while mesh is off
    if (mesh_get_state() != MESH_STATE_ON) {
        return info;
    }
    info.running = true;
    char id[MESH_BACKEND_ID_MAX_LENGTH + 1];
    if (mesh_get_backend(id, sizeof(id)) != ERROR_NONE) {
        return info;
    }
    char radio[MESH_NAME_MAX_LENGTH + 1];
    if (mesh_get_radio(radio, sizeof(radio)) != ERROR_NONE) {
        return info;
    }
    const size_t count = mesh_get_radio_count();
    for (size_t i = 0; i < count; i++) {
        char name[MESH_NAME_MAX_LENGTH + 1];
        MeshRadioKind kind;
        if (mesh_get_radio_name(i, name, sizeof(name)) == ERROR_NONE && strcmp(name, radio) == 0 && mesh_get_radio_kind(i, &kind) == ERROR_NONE) {
            char preset[MESH_NAME_MAX_LENGTH + 1] = {};
            if (mesh_get_preset(id, preset, sizeof(preset)) == ERROR_NONE && preset[0] != '\0' && strcmp(preset, "Default") != 0) {
                snprintf(info.network, sizeof(info.network), "%s (%s)", getNetworkLabel(id), preset);
            } else {
                snprintf(info.network, sizeof(info.network), "%s", getNetworkLabel(id));
            }
            snprintf(info.modulation, sizeof(info.modulation), "%s", kind == MESH_RADIO_LORA ? "LoRa" : "ESP-NOW");
            break;
        }
    }
    return info;
}

// The spans: "Using ", the network, " via ", the modulation. While mesh is off: "Mesh is ", "OFF", "", ""
constexpr int32_t NETWORK_SPAN = 1;
constexpr int32_t MODULATION_SPAN = 3;

lv_obj_t* createNetworkHeader(lv_obj_t* parent) {
    auto* header = lv_spangroup_create(parent);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    // Lines up with the cards: the content's padding plus the card's margin, which is the theme's container padding
    auto* probe = lv_obj_create(parent);
    const int32_t cardMargin = lv_obj_get_style_pad_left(probe, LV_PART_MAIN);
    lv_obj_delete(probe);
    lv_obj_set_style_pad_hor(header, CONTENT_PADDING + cardMargin, LV_STATE_DEFAULT);

    const auto secondary = lv_theme_get_color_secondary(header);
    for (const char* text : { "Using ", "", " via ", "" }) {
        auto* span = lv_spangroup_add_span(header);
        lv_span_set_text_static(span, text);
    }
    lv_style_set_text_color(lv_span_get_style(lv_spangroup_get_child(header, NETWORK_SPAN)), secondary);
    lv_style_set_text_color(lv_span_get_style(lv_spangroup_get_child(header, MODULATION_SPAN)), secondary);
    return header;
}

void updateNetworkHeader(lv_obj_t* header, const NetworkInfo& info) {
    lv_obj_set_hidden(header, info.running && info.network[0] == '\0');
    lv_span_set_text(lv_spangroup_get_child(header, 0), info.running ? tr("Using ") : tr("Mesh is "));
    lv_span_set_text(lv_spangroup_get_child(header, NETWORK_SPAN), info.running ? info.network : tr("OFF"));
    lv_span_set_text(lv_spangroup_get_child(header, 2), info.running ? tr(" via ") : "");
    lv_span_set_text(lv_spangroup_get_child(header, MODULATION_SPAN), info.modulation);
    lv_spangroup_refresh(header);
}

lv_obj_t* createContent(lv_obj_t* parent) {
    auto* content = lv_obj_create(parent);
    // The theme's container padding is the margin of the cards, read before it is overridden below
    lv_obj_set_user_data(content, reinterpret_cast<void*>(static_cast<intptr_t>(lv_obj_get_style_pad_left(content, LV_PART_MAIN))));
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, CONTENT_PADDING, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(content, CONTENT_PADDING, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(content, 0, LV_STATE_DEFAULT);
    return content;
}

lv_obj_t* createCard(lv_obj_t* parent) {
    auto* card = lvgl_card_create(parent);
    // A percentage width ignores the margin, so the width is the available width without the margins
    const auto margin = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(parent)));
    lv_obj_update_layout(parent);
    lv_obj_set_size(card, lv_obj_get_content_width(parent) - 2 * margin, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_all(card, margin, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(card, 0, LV_STATE_DEFAULT);
    // The items' backgrounds and dividers follow the card's rounded corners
    lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);
    return card;
}


}
