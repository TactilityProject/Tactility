#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/usb_host_hid.h>
#include <tactility/log.h>

#include <atomic>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <usb/hid_host.h>
#include <usb/hid_usage_keyboard.h>
#include <usb/hid_usage_mouse.h>
#include <esp_timer.h>

#include <lvgl.h>
#include <esp_lvgl_port.h>

#define USB_HID_CURSOR_PATH "A:/system/cursor.png"

namespace tt::lvgl {
    void hardware_keyboard_set_indev(lv_indev_t* device);
}

#define TAG "esp32_usbhost_hid"

typedef struct {
    uint32_t lv_key;
    bool pressed;
} key_event_t;

constexpr auto KEY_QUEUE_SIZE        = 64;
constexpr uint32_t KEY_REPEAT_DELAY_MS = 500;
constexpr uint32_t KEY_REPEAT_RATE_MS  = 50;
constexpr auto HID_EVENT_QUEUE_SIZE  = 8;
constexpr auto HID_PROC_TASK_STACK   = 4096;
constexpr auto HID_PROC_TASK_PRIORITY = 5;
constexpr auto HID_STOP_TIMEOUT_MS   = 2000;

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void* arg;
} hid_dev_event_t;

struct UsbHidCtx {
    std::atomic<int32_t> mouse_x{0};
    std::atomic<int32_t> mouse_y{0};
    std::atomic<bool>    mouse_pressed{false};
    std::atomic<bool>    mouse_connected{false};
    std::atomic<bool>    device_connected{false};  // true when any HID device (kb or mouse) is open
    QueueHandle_t        key_queue          = nullptr;
    QueueHandle_t        hid_event_queue    = nullptr;
    TaskHandle_t         hid_proc_task      = nullptr;
    SemaphoreHandle_t    hid_proc_task_done = nullptr;
    std::atomic<bool>    hid_proc_running{false};

    lv_indev_t* mouse_indev   = nullptr;
    lv_indev_t* kb_indev      = nullptr;
    lv_obj_t*   mouse_cursor  = nullptr;

    uint8_t  prev_keys[HID_KEYBOARD_KEY_MAX] = {};
    uint32_t pressed_lv_keys[256]            = {};
    bool caps_lock_active   = false;
    bool num_lock_active    = true;
    bool scroll_lock_active = false;

    std::atomic<hid_host_device_handle_t> kb_handle{nullptr};
    std::atomic<bool> kb_led_pending{false};

    std::atomic<uint32_t> repeat_lv_key{0};
    std::atomic<uint32_t> repeat_start_ms{0};

    bool     prev_mouse_button2   = false;
    bool     emit_repeat_release  = false;
    uint32_t repeat_release_key   = 0;
    std::atomic<uint32_t> repeat_last_ms{0};
};

static const uint8_t keycode2ascii[57][2] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'}, {'e', 'E'},
    {'f', 'F'}, {'g', 'G'}, {'h', 'H'}, {'i', 'I'}, {'j', 'J'},
    {'k', 'K'}, {'l', 'L'}, {'m', 'M'}, {'n', 'N'}, {'o', 'O'},
    {'p', 'P'}, {'q', 'Q'}, {'r', 'R'}, {'s', 'S'}, {'t', 'T'},
    {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'}, {'y', 'Y'},
    {'z', 'Z'},
    {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'},
    {'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'},
    {'\r', '\r'}, {0, 0}, {'\b', 0}, {'\t', '\t'}, {' ', ' '},
    {'-', '_'}, {'=', '+'}, {'[', '{'}, {']', '}'},
    {'\\', '|'}, {'\\', '|'}, {';', ':'}, {'\'', '"'},
    {'`', '~'}, {',', '<'}, {'.', '>'}, {'/', '?'},
};

static uint32_t hid_keycode_to_lv_key(uint8_t modifier, uint8_t key_code,
                                       bool caps_lock, bool num_lock) {
    bool shift = (modifier & HID_LEFT_SHIFT) || (modifier & HID_RIGHT_SHIFT);
    bool ctrl  = (modifier & HID_LEFT_CONTROL) || (modifier & HID_RIGHT_CONTROL);
    bool alt   = (modifier & HID_LEFT_ALT) || (modifier & HID_RIGHT_ALT);

    switch (key_code) {
        case HID_KEY_ENTER:         return LV_KEY_ENTER;
        case HID_KEY_ESC:           return LV_KEY_ESC;
        case HID_KEY_DEL:           return LV_KEY_BACKSPACE;
        case HID_KEY_DELETE:        return LV_KEY_DEL;
        case HID_KEY_TAB:           return shift ? LV_KEY_PREV : LV_KEY_NEXT;
        case HID_KEY_UP:            return LV_KEY_UP;
        case HID_KEY_DOWN:          return LV_KEY_DOWN;
        case HID_KEY_LEFT:          return LV_KEY_LEFT;
        case HID_KEY_RIGHT:         return LV_KEY_RIGHT;
        case HID_KEY_HOME:          return LV_KEY_HOME;
        case HID_KEY_END:           return LV_KEY_END;
        case HID_KEY_KEYPAD_ENTER:  return LV_KEY_ENTER;
        case HID_KEY_KEYPAD_ADD:    return '+';
        case HID_KEY_KEYPAD_SUB:    return '-';
        case HID_KEY_KEYPAD_MUL:    return '*';
        case HID_KEY_KEYPAD_DIV:    return '/';
        case HID_KEY_KEYPAD_0:      return num_lock ? (uint32_t)'0' : 0u;
        case HID_KEY_KEYPAD_1:      return num_lock ? (uint32_t)'1' : (uint32_t)LV_KEY_END;
        case HID_KEY_KEYPAD_2:      return num_lock ? (uint32_t)'2' : (uint32_t)LV_KEY_DOWN;
        case HID_KEY_KEYPAD_3:      return num_lock ? (uint32_t)'3' : 0u;
        case HID_KEY_KEYPAD_4:      return num_lock ? (uint32_t)'4' : (uint32_t)LV_KEY_LEFT;
        case HID_KEY_KEYPAD_5:      return num_lock ? (uint32_t)'5' : 0u;
        case HID_KEY_KEYPAD_6:      return num_lock ? (uint32_t)'6' : (uint32_t)LV_KEY_RIGHT;
        case HID_KEY_KEYPAD_7:      return num_lock ? (uint32_t)'7' : (uint32_t)LV_KEY_HOME;
        case HID_KEY_KEYPAD_8:      return num_lock ? (uint32_t)'8' : (uint32_t)LV_KEY_UP;
        case HID_KEY_KEYPAD_9:      return num_lock ? (uint32_t)'9' : 0u;
        case HID_KEY_KEYPAD_DELETE: return num_lock ? (uint32_t)'.' : (uint32_t)LV_KEY_DEL;
        default: break;
    }

    if (ctrl || alt) return 0;

    if (key_code < (sizeof(keycode2ascii) / sizeof(keycode2ascii[0]))) {
        bool is_letter = (key_code >= 0x04 && key_code <= 0x1D);
        bool effective_shift = is_letter ? (shift ^ caps_lock) : shift;
        uint8_t ch = keycode2ascii[key_code][effective_shift ? 1 : 0];
        if (ch != 0) return (uint32_t)ch;
    }
    return 0;
}

static void enqueue_scroll(UsbHidCtx* ctx, uint32_t scroll_key, int ticks) {
    if (!ctx->key_queue) return;
    for (int t = 0; t < ticks; t++) {
        key_event_t press   = { scroll_key, true  };
        key_event_t release = { scroll_key, false };
        xQueueSend(ctx->key_queue, &press,   0);
        xQueueSend(ctx->key_queue, &release, 0);
    }
}

static void hid_interface_callback(hid_host_device_handle_t handle,
                                   const hid_host_interface_event_t event,
                                   void* arg)
{
    auto* ctx = static_cast<UsbHidCtx*>(arg);
    uint8_t data[64] = {};
    size_t data_len = 0;
    hid_host_dev_params_t params;

    if (hid_host_device_get_params(handle, &params) != ESP_OK) return;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        if (hid_host_device_get_raw_input_report_data(handle, data, sizeof(data), &data_len) != ESP_OK) break;

        if (params.proto == HID_PROTOCOL_KEYBOARD) {
            if (data_len < sizeof(hid_keyboard_input_report_boot_t)) break;
            auto* kb = reinterpret_cast<const hid_keyboard_input_report_boot_t*>(data);

            for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
                uint8_t prev_hid = ctx->prev_keys[i];
                if (prev_hid > HID_KEY_ERROR_UNDEFINED) {
                    bool still_pressed = false;
                    for (int j = 0; j < HID_KEYBOARD_KEY_MAX; j++) {
                        if (kb->key[j] == prev_hid) { still_pressed = true; break; }
                    }
                    if (!still_pressed) {
                        uint32_t lv_key = ctx->pressed_lv_keys[prev_hid];
                        ctx->pressed_lv_keys[prev_hid] = 0;
                        if (lv_key && ctx->key_queue) {
                            key_event_t evt = { lv_key, false };
                            xQueueSend(ctx->key_queue, &evt, 0);
                        }
                        if (lv_key && lv_key == ctx->repeat_lv_key.load()) {
                            ctx->repeat_lv_key.store(0);
                        }
                    }
                }

                uint8_t hid_code = kb->key[i];
                if (hid_code > HID_KEY_ERROR_UNDEFINED) {
                    bool was_pressed = false;
                    for (int j = 0; j < HID_KEYBOARD_KEY_MAX; j++) {
                        if (ctx->prev_keys[j] == hid_code) { was_pressed = true; break; }
                    }
                    if (!was_pressed) {
                        if (hid_code == HID_KEY_CAPS_LOCK) {
                            ctx->caps_lock_active = !ctx->caps_lock_active;
                            ctx->kb_led_pending.store(true);
                            continue;
                        }
                        if (hid_code == HID_KEY_NUM_LOCK) {
                            ctx->num_lock_active = !ctx->num_lock_active;
                            ctx->kb_led_pending.store(true);
                            continue;
                        }
                        if (hid_code == HID_KEY_SCROLL_LOCK) {
                            ctx->scroll_lock_active = !ctx->scroll_lock_active;
                            ctx->kb_led_pending.store(true);
                            continue;
                        }
                        bool is_pgup = (hid_code == HID_KEY_PAGEUP) ||
                                       (!ctx->num_lock_active && hid_code == HID_KEY_KEYPAD_9);
                        bool is_pgdn = (hid_code == HID_KEY_PAGEDOWN) ||
                                       (!ctx->num_lock_active && hid_code == HID_KEY_KEYPAD_3);
                        if (is_pgup || is_pgdn) {
                            enqueue_scroll(ctx, is_pgup ? LV_KEY_UP : LV_KEY_DOWN, 8);
                            continue;
                        }
                        uint32_t lv_key = hid_keycode_to_lv_key(kb->modifier.val, hid_code,
                                                                  ctx->caps_lock_active, ctx->num_lock_active);
                        if (lv_key && ctx->key_queue) {
                            key_event_t evt = { lv_key, true };
                            xQueueSend(ctx->key_queue, &evt, 0);
                            ctx->pressed_lv_keys[hid_code] = lv_key;
                            ctx->repeat_lv_key.store(lv_key);
                            ctx->repeat_start_ms.store((uint32_t)(esp_timer_get_time() / 1000));
                            ctx->repeat_last_ms.store(0);
                        }
                    }
                }
            }
            memcpy(ctx->prev_keys, kb->key, HID_KEYBOARD_KEY_MAX);

        } else if (params.proto == HID_PROTOCOL_MOUSE) {
            if (data_len < sizeof(hid_mouse_input_report_boot_t)) break;
            auto* ms = reinterpret_cast<const hid_mouse_input_report_boot_t*>(data);
            lv_display_t* disp = lv_display_get_default();
            if (disp) {
                constexpr int32_t CURSOR_SIZE = 16;
                int32_t w = lv_display_get_original_horizontal_resolution(disp);
                int32_t h = lv_display_get_original_vertical_resolution(disp);
                int32_t nx = ctx->mouse_x + ms->x_displacement;
                int32_t ny = ctx->mouse_y + ms->y_displacement;
                if (nx < 0) nx = 0;
                if (nx > w - CURSOR_SIZE - 1) nx = w - CURSOR_SIZE - 1;
                if (ny < 0) ny = 0;
                if (ny > h - CURSOR_SIZE - 1) ny = h - CURSOR_SIZE - 1;
                ctx->mouse_x = nx;
                ctx->mouse_y = ny;
            }
            ctx->mouse_pressed = ms->buttons.button1;

            if (ms->buttons.button2 != ctx->prev_mouse_button2) {
                key_event_t evt = { LV_KEY_ESC, ms->buttons.button2 != 0 };
                if (ctx->key_queue) xQueueSend(ctx->key_queue, &evt, 0);
                ctx->prev_mouse_button2 = ms->buttons.button2 != 0;
            }

            if (data_len > sizeof(hid_mouse_input_report_boot_t) && ctx->key_queue) {
                int8_t wheel = (int8_t)data[sizeof(hid_mouse_input_report_boot_t)];
                if (wheel != 0) {
                    uint32_t scroll_key = (wheel < 0) ? LV_KEY_UP : LV_KEY_DOWN;
                    int ticks = (wheel < 0) ? -wheel : wheel;
                    if (ticks > 8) ticks = 8;
                    enqueue_scroll(ctx, scroll_key, ticks);
                }
            }
        }
        break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        if (params.proto == HID_PROTOCOL_KEYBOARD) {
            memset(ctx->prev_keys, 0, sizeof(ctx->prev_keys));
            memset(ctx->pressed_lv_keys, 0, sizeof(ctx->pressed_lv_keys));
            ctx->repeat_lv_key.store(0);
            ctx->kb_handle.store(nullptr);
            ctx->kb_led_pending.store(false);
            if (lvgl_port_lock(pdMS_TO_TICKS(200))) {
                tt::lvgl::hardware_keyboard_set_indev(nullptr);
                lvgl_port_unlock();
            } else {
                LOG_W(TAG, "keyboard disconnect: LVGL lock timeout, keyboard indev not unregistered");
            }
        } else if (params.proto == HID_PROTOCOL_MOUSE) {
            ctx->mouse_connected = false;
            if (ctx->mouse_cursor && lvgl_port_lock(0)) {
                lv_obj_add_flag(ctx->mouse_cursor, LV_OBJ_FLAG_HIDDEN);
                lvgl_port_unlock();
            }
        }
        hid_host_device_close(handle);
        if (!ctx->kb_handle.load() && !ctx->mouse_connected) {
            ctx->device_connected = false;
        }
        break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        LOG_W(TAG, "HID transfer error (proto=%d)", params.proto);
        break;

    default:
        break;
    }
}

static void mouse_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* ctx = static_cast<UsbHidCtx*>(lv_indev_get_user_data(indev));
    int32_t cx = ctx->mouse_x;
    int32_t cy = ctx->mouse_y;

    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        int32_t ow = lv_display_get_original_horizontal_resolution(disp);
        int32_t oh = lv_display_get_original_vertical_resolution(disp);
        switch (lv_display_get_rotation(disp)) {
            case LV_DISPLAY_ROTATION_0:
                data->point.x = (lv_coord_t)cx;
                data->point.y = (lv_coord_t)cy;
                break;
            case LV_DISPLAY_ROTATION_90:
                data->point.x = (lv_coord_t)cy;
                data->point.y = (lv_coord_t)(oh - cx - 1);
                break;
            case LV_DISPLAY_ROTATION_180:
                data->point.x = (lv_coord_t)(ow - cx - 1);
                data->point.y = (lv_coord_t)(oh - cy - 1);
                break;
            case LV_DISPLAY_ROTATION_270:
                data->point.x = (lv_coord_t)(ow - cy - 1);
                data->point.y = (lv_coord_t)cx;
                break;
        }
    } else {
        data->point.x = (lv_coord_t)cx;
        data->point.y = (lv_coord_t)cy;
    }

    data->state = ctx->mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void keyboard_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* ctx = static_cast<UsbHidCtx*>(lv_indev_get_user_data(indev));

    if (ctx->emit_repeat_release) {
        ctx->emit_repeat_release = false;
        data->key = ctx->repeat_release_key;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    key_event_t evt;
    if (ctx->key_queue && xQueueReceive(ctx->key_queue, &evt, 0) == pdTRUE) {
        data->key = evt.lv_key;
        data->state = evt.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = (uxQueueMessagesWaiting(ctx->key_queue) > 0);
        return;
    }

    uint32_t rkey = ctx->repeat_lv_key.load();
    if (rkey != 0) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t elapsed = now_ms - ctx->repeat_start_ms.load();
        if (elapsed >= KEY_REPEAT_DELAY_MS) {
            uint32_t last = ctx->repeat_last_ms.load();
            if (last == 0 || (now_ms - last) >= KEY_REPEAT_RATE_MS) {
                ctx->repeat_last_ms.store(now_ms);
                ctx->emit_repeat_release = true;
                ctx->repeat_release_key  = rkey;
                data->key   = rkey;
                data->state = LV_INDEV_STATE_PRESSED;
                data->continue_reading = true;
                return;
            }
        }
    }

    data->state = LV_INDEV_STATE_RELEASED;
}

static void hid_driver_callback(hid_host_device_handle_t handle,
                                const hid_host_driver_event_t event,
                                void* arg)
{
    auto* ctx = static_cast<UsbHidCtx*>(arg);
    hid_dev_event_t evt = { handle, event, arg };
    if (ctx->hid_event_queue) {
        xQueueSend(ctx->hid_event_queue, &evt, 0);
    }
}

static void hidProcTask(void* arg) {
    auto* ctx = static_cast<UsbHidCtx*>(arg);
    LOG_I(TAG, "HID proc task started");

    while (!lv_is_initialized()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (lvgl_port_lock(0)) {
        ctx->mouse_cursor = lv_image_create(lv_layer_sys());
        lv_obj_remove_flag(ctx->mouse_cursor, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(ctx->mouse_cursor, USB_HID_CURSOR_PATH);
        lv_obj_add_flag(ctx->mouse_cursor, LV_OBJ_FLAG_HIDDEN);

        ctx->mouse_indev = lv_indev_create();
        lv_indev_set_type(ctx->mouse_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(ctx->mouse_indev, mouse_read_cb);
        lv_indev_set_user_data(ctx->mouse_indev, ctx);
        lv_indev_set_cursor(ctx->mouse_indev, ctx->mouse_cursor);

        ctx->kb_indev = lv_indev_create();
        lv_indev_set_type(ctx->kb_indev, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(ctx->kb_indev, keyboard_read_cb);
        lv_indev_set_user_data(ctx->kb_indev, ctx);
        lv_indev_set_group(ctx->kb_indev, lv_group_get_default());
        lvgl_port_unlock();
        LOG_I(TAG, "LVGL input devices registered");
    } else {
        LOG_W(TAG, "could not acquire LVGL lock for indev registration");
    }

    while (ctx->hid_proc_running) {
        hid_dev_event_t dev_evt;
        if (xQueueReceive(ctx->hid_event_queue, &dev_evt, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (ctx->kb_led_pending.exchange(false) && ctx->kb_handle.load()) {
                uint8_t leds = (ctx->num_lock_active    ? 0x01 : 0)
                             | (ctx->caps_lock_active   ? 0x02 : 0)
                             | (ctx->scroll_lock_active ? 0x04 : 0);
                hid_class_request_set_report(ctx->kb_handle.load(), HID_REPORT_TYPE_OUTPUT, 0, &leds, 1);
            }
            continue;
        }
        if (dev_evt.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
            hid_host_dev_params_t params;
            if (hid_host_device_get_params(dev_evt.handle, &params) != ESP_OK) continue;

            if (params.proto != HID_PROTOCOL_KEYBOARD && params.proto != HID_PROTOCOL_MOUSE) {
                LOG_D(TAG, "ignoring HID interface with unhandled proto=%d", params.proto);
                continue;
            }
            LOG_I(TAG, "HID device connected (proto=%d)", params.proto);

            const hid_host_device_config_t dev_cfg = {
                .callback = hid_interface_callback,
                .callback_arg = ctx,
            };
            if (hid_host_device_open(dev_evt.handle, &dev_cfg) != ESP_OK) {
                LOG_W(TAG, "hid_host_device_open failed");
                continue;
            }
            hid_class_request_set_protocol(dev_evt.handle, HID_REPORT_PROTOCOL_BOOT);
            if (params.proto == HID_PROTOCOL_KEYBOARD) {
                hid_class_request_set_idle(dev_evt.handle, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                ctx->kb_handle.store(dev_evt.handle);
                uint8_t leds = (ctx->num_lock_active    ? 0x01 : 0)
                             | (ctx->caps_lock_active   ? 0x02 : 0)
                             | (ctx->scroll_lock_active ? 0x04 : 0);
                hid_class_request_set_report(dev_evt.handle, HID_REPORT_TYPE_OUTPUT, 0, &leds, 1);
                if (ctx->kb_indev && lvgl_port_lock(0)) {
                    tt::lvgl::hardware_keyboard_set_indev(ctx->kb_indev);
                    lvgl_port_unlock();
                }
            } else if (params.proto == HID_PROTOCOL_MOUSE) {
                ctx->mouse_connected = true;
                if (ctx->mouse_cursor && lvgl_port_lock(0)) {
                    lv_obj_remove_flag(ctx->mouse_cursor, LV_OBJ_FLAG_HIDDEN);
                    lvgl_port_unlock();
                }
            }
            ctx->device_connected = true;
            hid_host_device_start(dev_evt.handle);
        }
    }

    if (lvgl_port_lock(0)) {
        if (ctx->mouse_indev)  { lv_indev_delete(ctx->mouse_indev);  ctx->mouse_indev  = nullptr; }
        if (ctx->mouse_cursor) { lv_obj_delete(ctx->mouse_cursor);   ctx->mouse_cursor = nullptr; }
        if (ctx->kb_indev)     {
            tt::lvgl::hardware_keyboard_set_indev(nullptr);
            lv_indev_delete(ctx->kb_indev);
            ctx->kb_indev = nullptr;
        }
        lvgl_port_unlock();
    }

    LOG_I(TAG, "HID proc task stopped");
    xSemaphoreGive(ctx->hid_proc_task_done);
    vTaskDelete(nullptr);
}

static bool api_hid_is_connected(struct Device* device) {
    auto* ctx = static_cast<UsbHidCtx*>(device_get_driver_data(device));
    return ctx && ctx->device_connected.load();
}

static const UsbHidApi hid_api = {
    .is_connected = api_hid_is_connected,
};

extern "C" {

static error_t start_device(struct Device* device) {
    auto* ctx = new UsbHidCtx();

    ctx->key_queue = xQueueCreate(KEY_QUEUE_SIZE, sizeof(key_event_t));
    if (!ctx->key_queue) {
        LOG_E(TAG, "failed to create key queue");
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->hid_event_queue = xQueueCreate(HID_EVENT_QUEUE_SIZE, sizeof(hid_dev_event_t));
    if (!ctx->hid_event_queue) {
        LOG_E(TAG, "failed to create HID event queue");
        vQueueDelete(ctx->key_queue);
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->hid_proc_task_done = xSemaphoreCreateBinary();
    if (!ctx->hid_proc_task_done) {
        LOG_E(TAG, "failed to create task done semaphore");
        vQueueDelete(ctx->key_queue);
        vQueueDelete(ctx->hid_event_queue);
        delete ctx;
        return ERROR_RESOURCE;
    }

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority          = HID_PROC_TASK_PRIORITY,
        .stack_size             = HID_PROC_TASK_STACK,
        .core_id                = tskNO_AFFINITY,
        .callback               = hid_driver_callback,
        .callback_arg           = ctx,
    };
    if (hid_host_install(&hid_cfg) != ESP_OK) {
        LOG_E(TAG, "hid_host_install failed");
        vQueueDelete(ctx->key_queue);
        vQueueDelete(ctx->hid_event_queue);
        vSemaphoreDelete(ctx->hid_proc_task_done);
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->hid_proc_running = true;
    BaseType_t result = xTaskCreate(hidProcTask, "hid_proc", HID_PROC_TASK_STACK,
                                    ctx, HID_PROC_TASK_PRIORITY, &ctx->hid_proc_task);
    if (result != pdPASS) {
        LOG_E(TAG, "failed to create hid_proc task");
        ctx->hid_proc_running = false;
        hid_host_uninstall();
        vQueueDelete(ctx->key_queue);
        vQueueDelete(ctx->hid_event_queue);
        vSemaphoreDelete(ctx->hid_proc_task_done);
        delete ctx;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, ctx);
    LOG_I(TAG, "started");
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbHidCtx*>(device_get_driver_data(device));
    if (!ctx) return ERROR_NONE;

    ctx->hid_proc_running = false;

    if (xSemaphoreTake(ctx->hid_proc_task_done, pdMS_TO_TICKS(HID_STOP_TIMEOUT_MS)) != pdTRUE) {
        // Acquire LVGL lock BEFORE killing the task — the task may hold it.
        bool have_lvgl = lvgl_port_lock(pdMS_TO_TICKS(200));
        LOG_W(TAG, "HID proc task stop timed out, force terminating%s",
              have_lvgl ? "" : " (LVGL lock unavailable — resources may leak)");
        vTaskDelete(ctx->hid_proc_task);
        if (have_lvgl) {
            if (ctx->mouse_indev)  { lv_indev_delete(ctx->mouse_indev);  ctx->mouse_indev  = nullptr; }
            if (ctx->mouse_cursor) { lv_obj_delete(ctx->mouse_cursor);   ctx->mouse_cursor = nullptr; }
            if (ctx->kb_indev)     {
                tt::lvgl::hardware_keyboard_set_indev(nullptr);
                lv_indev_delete(ctx->kb_indev);
                ctx->kb_indev = nullptr;
            }
            lvgl_port_unlock();
        }
    }
    ctx->hid_proc_task = nullptr;
    vSemaphoreDelete(ctx->hid_proc_task_done);

    hid_host_uninstall();

    if (ctx->key_queue)       { vQueueDelete(ctx->key_queue);       ctx->key_queue       = nullptr; }
    if (ctx->hid_event_queue) { vQueueDelete(ctx->hid_event_queue); ctx->hid_event_queue = nullptr; }

    device_set_driver_data(device, nullptr);
    delete ctx;
    LOG_I(TAG, "stopped");
    return ERROR_NONE;
}

Driver esp32_usbhost_hid_driver = {
    .name         = "esp32_usbhost_hid",
    .compatible   = (const char*[]) { "espressif,esp32-usbhost-hid", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = &hid_api,
    .device_type  = &USB_HOST_HID_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"
