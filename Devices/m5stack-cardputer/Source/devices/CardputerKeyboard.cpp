#include "CardputerKeyboard.h"

#include <tactility/log.h>

constexpr auto* TAG = "Keyboard";

bool CardputerKeyboard::startLvgl(lv_display_t* display) {
    keyboard.init();

    lvglDevice = lv_indev_create();
    lv_indev_set_type(lvglDevice, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(lvglDevice, &readCallback);
    lv_indev_set_display(lvglDevice, display);
    lv_indev_set_user_data(lvglDevice, this);

    return true;
}

bool CardputerKeyboard::stopLvgl() {
    lv_indev_delete(lvglDevice);
    lvglDevice = nullptr;

    return true;
}

void CardputerKeyboard::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    CardputerKeyboard* self = static_cast<CardputerKeyboard*>(lv_indev_get_user_data(indev));

    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    self->keyboard.updateKeyList();

    if (self->keyboard.keyList().size() != self->lastKeyNum) {
        // If key pressed
        if (self->keyboard.keyList().size() != 0) {
            // Update states and values
            self->keyboard.updateKeysState();
            if (!self->keyboard.keysState().fn) {
                if (self->keyboard.keysState().enter) {
                    data->key = LV_KEY_ENTER;
                    data->state = LV_INDEV_STATE_PRESSED;
                } else if (self->keyboard.keysState().space) {
                    data->key = ' ';
                    data->state = LV_INDEV_STATE_PRESSED;
                } else if (self->keyboard.keysState().del) {
                    data->key = LV_KEY_BACKSPACE;
                    data->state = LV_INDEV_STATE_PRESSED;
                } else {
                    // Normal chars
                    for (auto& i : self->keyboard.keysState().values) {
                        data->key = i;
                        data->state = LV_INDEV_STATE_PRESSED;
                        break; // We only support 1 keypress for now
                    }
                }
            } else {
                if (self->keyboard.keysState().del) {
                    LOG_I(TAG, "del");
                    data->key = LV_KEY_DEL;
                    data->state = LV_INDEV_STATE_PRESSED;
                } else {
                    for (auto& i : self->keyboard.keysState().values) {
                        if (i == ';') { // Up
                            /*
                             * WARNING:
                             * lv_switch picks up on this and toggles it, while the CardputerEncoder uses it for scrolling.
                             * We disable the keypress so the encoder can work properly.
                             */
                            // TODO: Can we detect the active widget and ignore it in case it's a switch?
                            // data->key = LV_KEY_UP;
                            // data->state = LV_INDEV_STATE_PRESSED;
                        } else if (i == '.') { // Down
                            /*
                             * WARNING:
                             * lv_switch picks up on this and toggles it, while the CardputerEncoder uses it for scrolling.
                             * We disable the keypress so the encoder can work properly.
                             */
                            // TODO: Can we detect the active widget and ignore it in case it's a switch?
                            // data->key = LV_KEY_DOWN;
                            // data->state = LV_INDEV_STATE_PRESSED;
                        } else if (i == ',') { // Left
                            data->key = LV_KEY_LEFT;
                            data->state = LV_INDEV_STATE_PRESSED;
                        } else if (i == '/') { // Right
                            data->key = LV_KEY_RIGHT;
                            data->state = LV_INDEV_STATE_PRESSED;
                        } else if (i == '`') { // Escape
                            data->key = LV_KEY_ESC;
                            data->state = LV_INDEV_STATE_PRESSED;
                        }

                        break; // We only support 1 keypress for now
                    }
                }
            }
            self->lastKeyNum = self->keyboard.keyList().size();
        } else {
            self->lastKeyNum = 0;
        }
    }
}
