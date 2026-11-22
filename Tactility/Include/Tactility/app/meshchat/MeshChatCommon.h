#pragma once


#include <mesh/mesh.h>

#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

/** A macro, because the manifest's id is a char array that needs a string literal */
#define CONVERSATION_APP_ID "tactility.meshconversation"
#define ADD_CONTACT_APP_ID "tactility.meshaddcontact"
#define MESH_SETTINGS_APP_ID "tactility.meshsettings"
#define MESH_INFO_APP_ID "tactility.meshinfo"
#define CONTACT_DETAILS_APP_ID "tactility.meshcontactdetails"

namespace tt::app::meshchat {

/** Arguments of the conversation app: [0] kind, [1] contact address in hex or channel index, [2] title */
constexpr auto* KIND_CONTACT = "contact";
constexpr auto* KIND_CHANNEL = "channel";

/** Explains why the mesh service can't do anything, e.g. no region set or the radio failed to start */
inline ::std::string getNotReadyText() {
    return ::std::string("Mesh isn't running: ") + mesh_state_to_string(mesh_get_state()) + ". Check the Mesh settings.";
}

/** Whether mesh is running, and the network and the kind of radio that it uses. Those are empty while mesh isn't running. */
struct NetworkInfo {
    bool running = false;
    /** The network, followed by its preset in brackets when that isn't the default one */
    char network[MESH_BACKEND_ID_MAX_LENGTH + MESH_NAME_MAX_LENGTH + 4] = {};
    char modulation[16] = {};
};

/** Queries the service thread, so it's called without the LVGL lock */
NetworkInfo loadNetworkInfo();

/** A statusline under the toolbar: "[network] · [modulation]" while mesh is running, and a warning when it isn't */
lv_obj_t* createNetworkHeader(lv_obj_t* parent);

/** Requires the LVGL lock. Hides the header while mesh is running without a known network. */
void updateNetworkHeader(lv_obj_t* header, const NetworkInfo& info);

/** Starts an app by id with the given arguments */
void startApp(const char* id, int argc, const char* const argv[]);

/** A scrollable, borderless column below the toolbar and network header that holds the lists and buttons of a screen */
lv_obj_t* createContent(lv_obj_t* parent);

/**
 * A card that holds list items. The items are direct children of the card,
 * because every nesting level adds to the LVGL task's draw stack.
 * Its margin is the theme's container padding.
 * @param[in] parent the result of createContent()
 */
lv_obj_t* createCard(lv_obj_t* parent);

inline ::std::string addressToHex(const MeshAddress& address) {
    ::std::string text;
    char hex[3];
    for (size_t i = 0; i < address.length; i++) {
        snprintf(hex, sizeof(hex), "%02x", address.bytes[i]);
        text += hex;
    }
    return text;
}

inline bool hexToAddress(const char* hex, MeshAddress* address) {
    const size_t length = strlen(hex);
    if (length == 0 || length % 2 != 0 || length / 2 > MESH_ADDRESS_MAX_LENGTH) {
        return false;
    }
    *address = {};
    for (size_t i = 0; i < length / 2; i++) {
        char pair[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char* end = nullptr;
        address->bytes[i] = (uint8_t)strtoul(pair, &end, 16);
        if (end != pair + 2) {
            return false;
        }
    }
    address->length = (uint8_t)(length / 2);
    return true;
}

}

