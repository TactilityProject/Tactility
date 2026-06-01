#include <sdkconfig.h>
#ifdef CONFIG_SOC_USB_OTG_SUPPORTED

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

#define TAG "esp32_usbhost_hid"

constexpr auto HID_EVENT_QUEUE_SIZE   = 8;
constexpr auto HID_PROC_TASK_STACK    = 4096;
constexpr auto HID_PROC_TASK_PRIORITY = 5;
constexpr auto HID_STOP_TIMEOUT_MS    = 2000;
constexpr auto MAX_SUBSCRIBERS        = 4;

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
    void* arg;
} hid_dev_event_t;

struct UsbHidCtx {
    std::atomic<bool>    mouse_connected{false};
    std::atomic<bool>    device_connected{false};
    QueueHandle_t        hid_event_queue    = nullptr;
    TaskHandle_t         hid_proc_task      = nullptr;
    SemaphoreHandle_t    hid_proc_task_done = nullptr;
    std::atomic<bool>    hid_proc_running{false};

    uint8_t  prev_keys[HID_KEYBOARD_KEY_MAX] = {};
    uint32_t pressed_lv_keys[256]            = {};
    bool caps_lock_active   = false;
    bool num_lock_active    = true;
    bool scroll_lock_active = false;
    bool prev_mouse_button2 = false;

    std::atomic<hid_host_device_handle_t> kb_handle{nullptr};
    std::atomic<bool> kb_led_pending{false};

    QueueHandle_t    subscribers[MAX_SUBSCRIBERS] = {};
    SemaphoreHandle_t sub_mutex                   = nullptr;
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
        case HID_KEY_ENTER:         return USB_HID_KEY_ENTER;
        case HID_KEY_ESC:           return USB_HID_KEY_ESC;
        case HID_KEY_DEL:           return USB_HID_KEY_BACKSPACE;
        case HID_KEY_DELETE:        return USB_HID_KEY_DEL;
        case HID_KEY_TAB:           return shift ? USB_HID_KEY_PREV : USB_HID_KEY_NEXT;
        case HID_KEY_UP:            return USB_HID_KEY_UP;
        case HID_KEY_DOWN:          return USB_HID_KEY_DOWN;
        case HID_KEY_LEFT:          return USB_HID_KEY_LEFT;
        case HID_KEY_RIGHT:         return USB_HID_KEY_RIGHT;
        case HID_KEY_HOME:          return USB_HID_KEY_HOME;
        case HID_KEY_END:           return USB_HID_KEY_END;
        case HID_KEY_KEYPAD_ENTER:  return USB_HID_KEY_ENTER;
        case HID_KEY_KEYPAD_ADD:    return '+';
        case HID_KEY_KEYPAD_SUB:    return '-';
        case HID_KEY_KEYPAD_MUL:    return '*';
        case HID_KEY_KEYPAD_DIV:    return '/';
        case HID_KEY_KEYPAD_0:      return num_lock ? (uint32_t)'0' : 0u;
        case HID_KEY_KEYPAD_1:      return num_lock ? (uint32_t)'1' : (uint32_t)USB_HID_KEY_END;
        case HID_KEY_KEYPAD_2:      return num_lock ? (uint32_t)'2' : (uint32_t)USB_HID_KEY_DOWN;
        case HID_KEY_KEYPAD_3:      return num_lock ? (uint32_t)'3' : 0u;
        case HID_KEY_KEYPAD_4:      return num_lock ? (uint32_t)'4' : (uint32_t)USB_HID_KEY_LEFT;
        case HID_KEY_KEYPAD_5:      return num_lock ? (uint32_t)'5' : 0u;
        case HID_KEY_KEYPAD_6:      return num_lock ? (uint32_t)'6' : (uint32_t)USB_HID_KEY_RIGHT;
        case HID_KEY_KEYPAD_7:      return num_lock ? (uint32_t)'7' : (uint32_t)USB_HID_KEY_HOME;
        case HID_KEY_KEYPAD_8:      return num_lock ? (uint32_t)'8' : (uint32_t)USB_HID_KEY_UP;
        case HID_KEY_KEYPAD_9:      return num_lock ? (uint32_t)'9' : 0u;
        case HID_KEY_KEYPAD_DELETE: return num_lock ? (uint32_t)'.' : (uint32_t)USB_HID_KEY_DEL;
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

static void publish_event(UsbHidCtx* ctx, const UsbHidEvent* evt) {
    if (xSemaphoreTake(ctx->sub_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        LOG_W(TAG, "publish_event: sub_mutex contended, event type=%d dropped", (int)evt->type);
        return;
    }
    bool is_release = (evt->type == USB_HID_EVENT_KEY && !evt->key.pressed);
    TickType_t send_timeout = is_release ? pdMS_TO_TICKS(10) : 0;
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (ctx->subscribers[i]) {
            xQueueSend(ctx->subscribers[i], evt, send_timeout);
        }
    }
    xSemaphoreGive(ctx->sub_mutex);
}

static void publish_scroll(UsbHidCtx* ctx, int32_t delta) {
    UsbHidEvent evt = { .type = USB_HID_EVENT_SCROLL, .scroll = { delta } };
    publish_event(ctx, &evt);
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
                        if (lv_key) {
                            UsbHidEvent evt = { .type = USB_HID_EVENT_KEY, .key = { lv_key, false } };
                            publish_event(ctx, &evt);
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
                            publish_scroll(ctx, is_pgup ? -8 : 8);
                            continue;
                        }
                        uint32_t lv_key = hid_keycode_to_lv_key(kb->modifier.val, hid_code,
                                                                  ctx->caps_lock_active, ctx->num_lock_active);
                        if (lv_key) {
                            UsbHidEvent evt = { .type = USB_HID_EVENT_KEY, .key = { lv_key, true } };
                            publish_event(ctx, &evt);
                            ctx->pressed_lv_keys[hid_code] = lv_key;
                        }
                    }
                }
            }
            memcpy(ctx->prev_keys, kb->key, HID_KEYBOARD_KEY_MAX);

        } else if (params.proto == HID_PROTOCOL_MOUSE) {
            if (data_len < sizeof(hid_mouse_input_report_boot_t)) break;
            auto* ms = reinterpret_cast<const hid_mouse_input_report_boot_t*>(data);

            if (ms->x_displacement != 0 || ms->y_displacement != 0) {
                UsbHidEvent evt = { .type = USB_HID_EVENT_MOUSE_MOVE,
                                    .mouse_move = { (int32_t)ms->x_displacement, (int32_t)ms->y_displacement } };
                publish_event(ctx, &evt);
            }
            {
                bool b1 = ms->buttons.button1 != 0;
                bool b2 = ms->buttons.button2 != 0;
                UsbHidEvent evt = { .type = USB_HID_EVENT_MOUSE_BTN, .mouse_btn = { b1, b2 } };
                publish_event(ctx, &evt);

                if (b2 != ctx->prev_mouse_button2) {
                    UsbHidEvent key_evt = { .type = USB_HID_EVENT_KEY, .key = { USB_HID_KEY_ESC, b2 } };
                    publish_event(ctx, &key_evt);
                    ctx->prev_mouse_button2 = b2;
                }
            }

            if (data_len > sizeof(hid_mouse_input_report_boot_t)) {
                int8_t wheel = (int8_t)data[sizeof(hid_mouse_input_report_boot_t)];
                if (wheel != 0) {
                    int32_t delta = (wheel < -8) ? -8 : (wheel > 8) ? 8 : (int32_t)wheel;
                    publish_scroll(ctx, delta);
                }
            }
        }
        break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        if (params.proto == HID_PROTOCOL_KEYBOARD) {
            memset(ctx->prev_keys, 0, sizeof(ctx->prev_keys));
            memset(ctx->pressed_lv_keys, 0, sizeof(ctx->pressed_lv_keys));
            ctx->kb_handle.store(nullptr);
            ctx->kb_led_pending.store(false);
        } else if (params.proto == HID_PROTOCOL_MOUSE) {
            ctx->mouse_connected = false;
        }
        hid_host_device_close(handle);
        if (!ctx->kb_handle.load() && !ctx->mouse_connected) {
            ctx->device_connected = false;
        }
        {
            UsbHidEventType disc_type = (params.proto == HID_PROTOCOL_KEYBOARD)
                ? USB_HID_EVENT_KEYBOARD_DISCONNECTED
                : USB_HID_EVENT_MOUSE_DISCONNECTED;
            UsbHidEvent evt = { .type = disc_type };
            publish_event(ctx, &evt);
        }
        break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        LOG_W(TAG, "HID transfer error (proto=%d)", params.proto);
        break;

    default:
        break;
    }
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
            } else if (params.proto == HID_PROTOCOL_MOUSE) {
                ctx->mouse_connected = true;
            }
            ctx->device_connected = true;
            {
                UsbHidEventType conn_type = (params.proto == HID_PROTOCOL_KEYBOARD)
                    ? USB_HID_EVENT_KEYBOARD_CONNECTED
                    : USB_HID_EVENT_MOUSE_CONNECTED;
                UsbHidEvent evt = { .type = conn_type };
                publish_event(ctx, &evt);
            }
            hid_host_device_start(dev_evt.handle);
        }
    }

    LOG_I(TAG, "HID proc task stopped");
    xSemaphoreGive(ctx->hid_proc_task_done);
    vTaskDelete(nullptr);
}

static bool api_hid_is_connected(struct Device* device) {
    auto* ctx = static_cast<UsbHidCtx*>(device_get_driver_data(device));
    return ctx && ctx->device_connected.load();
}

static bool api_hid_subscribe(struct Device* device, UsbHidQueueHandle event_queue) {
    auto* ctx = static_cast<UsbHidCtx*>(device_get_driver_data(device));
    if (!ctx || !event_queue) return false;
    if (xSemaphoreTake(ctx->sub_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool added = false;
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!ctx->subscribers[i]) {
            ctx->subscribers[i] = static_cast<QueueHandle_t>(event_queue);
            added = true;
            break;
        }
    }
    xSemaphoreGive(ctx->sub_mutex);
    if (!added) LOG_W(TAG, "subscriber list full");
    return added;
}

static void api_hid_unsubscribe(struct Device* device, UsbHidQueueHandle event_queue) {
    auto* ctx = static_cast<UsbHidCtx*>(device_get_driver_data(device));
    if (!ctx || !event_queue) return;
    if (xSemaphoreTake(ctx->sub_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_W(TAG, "unsubscribe: mutex timeout, subscriber slot may remain stale");
        return;
    }
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (ctx->subscribers[i] == static_cast<QueueHandle_t>(event_queue)) {
            ctx->subscribers[i] = nullptr;
            break;
        }
    }
    xSemaphoreGive(ctx->sub_mutex);
}

static const UsbHidApi hid_api = {
    .is_connected = api_hid_is_connected,
    .subscribe    = api_hid_subscribe,
    .unsubscribe  = api_hid_unsubscribe,
};

extern "C" {

static error_t start_device(struct Device* device) {
    auto* ctx = new UsbHidCtx();

    ctx->sub_mutex = xSemaphoreCreateMutex();
    if (!ctx->sub_mutex) {
        LOG_E(TAG, "failed to create subscriber mutex");
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->hid_event_queue = xQueueCreate(HID_EVENT_QUEUE_SIZE, sizeof(hid_dev_event_t));
    if (!ctx->hid_event_queue) {
        LOG_E(TAG, "failed to create HID event queue");
        vSemaphoreDelete(ctx->sub_mutex);
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->hid_proc_task_done = xSemaphoreCreateBinary();
    if (!ctx->hid_proc_task_done) {
        LOG_E(TAG, "failed to create task done semaphore");
        vQueueDelete(ctx->hid_event_queue);
        vSemaphoreDelete(ctx->sub_mutex);
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
        vQueueDelete(ctx->hid_event_queue);
        vSemaphoreDelete(ctx->hid_proc_task_done);
        vSemaphoreDelete(ctx->sub_mutex);
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
        vQueueDelete(ctx->hid_event_queue);
        vSemaphoreDelete(ctx->hid_proc_task_done);
        vSemaphoreDelete(ctx->sub_mutex);
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
        LOG_W(TAG, "HID proc task stop timed out, force terminating");
        vTaskDelete(ctx->hid_proc_task);
    }
    ctx->hid_proc_task = nullptr;
    vSemaphoreDelete(ctx->hid_proc_task_done);

    hid_host_uninstall();

    if (ctx->hid_event_queue) { vQueueDelete(ctx->hid_event_queue); ctx->hid_event_queue = nullptr; }
    if (ctx->sub_mutex)       { vSemaphoreDelete(ctx->sub_mutex);   ctx->sub_mutex        = nullptr; }

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

#endif // CONFIG_SOC_USB_OTG_SUPPORTED
