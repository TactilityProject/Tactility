#include <Tactility/lvgl/UsbHidInput.h>

#ifdef ESP_PLATFORM

#include <atomic>
#include <Tactility/Assets.h>
#include <Tactility/lvgl/Keyboard.h>
#include <Tactility/lvgl/LvglSync.h>

#include <tactility/device.h>
#include <tactility/drivers/usb_host_hid.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <lvgl.h>

namespace tt::lvgl {

constexpr auto* TAG = "UsbHidInput";

constexpr auto HID_EVENT_QUEUE_SIZE    = 64;
constexpr auto KEY_EVENT_QUEUE_SIZE    = 64;
constexpr auto TASK_STACK              = 3072;
constexpr auto TASK_PRIORITY           = 5;
constexpr auto STOP_TIMEOUT_MS         = 2000;
constexpr uint32_t KEY_REPEAT_DELAY_MS = 500;
constexpr uint32_t KEY_REPEAT_RATE_MS  = 50;
constexpr int32_t CURSOR_SIZE = 16;

typedef struct {
    uint32_t lv_key;
    bool pressed;
} KeyEvent;

struct UsbHidInputCtx {
    // Receives raw UsbHidEvent items from the HID driver
    QueueHandle_t      hid_queue     = nullptr;
    // Key-only events forwarded to the keyboard read callback
    QueueHandle_t      key_queue     = nullptr;
    TaskHandle_t       task          = nullptr;
    SemaphoreHandle_t  task_done     = nullptr;
    std::atomic<bool>  running{false};
    std::atomic<bool>  subscribed{false};

    lv_indev_t* mouse_indev   = nullptr;
    lv_indev_t* kb_indev      = nullptr;
    lv_obj_t*   mouse_cursor  = nullptr;

    std::atomic<int32_t> mouse_x{0};
    std::atomic<int32_t> mouse_y{0};
    std::atomic<bool>    mouse_btn1{false};
    bool    mouse_connected = false;

    uint32_t repeat_lv_key       = 0;
    uint32_t repeat_start_ms     = 0;
    uint32_t repeat_last_ms      = 0;
    bool     emit_repeat_release = false;
    uint32_t repeat_release_key  = 0;
};

static UsbHidInputCtx* s_ctx = nullptr;

static void mouse_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* ctx = static_cast<UsbHidInputCtx*>(lv_indev_get_user_data(indev));
    int32_t cx = ctx->mouse_x.load();
    int32_t cy = ctx->mouse_y.load();

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

    data->state = ctx->mouse_btn1.load() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void keyboard_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* ctx = static_cast<UsbHidInputCtx*>(lv_indev_get_user_data(indev));

    if (ctx->emit_repeat_release) {
        ctx->emit_repeat_release = false;
        data->key = ctx->repeat_release_key;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    KeyEvent evt;
    if (ctx->key_queue && xQueueReceive(ctx->key_queue, &evt, 0) == pdTRUE) {
        data->key = evt.lv_key;
        data->state = evt.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (evt.pressed) {
            ctx->repeat_lv_key   = evt.lv_key;
            ctx->repeat_start_ms = lv_tick_get();
            ctx->repeat_last_ms  = 0;
        } else if (evt.lv_key == ctx->repeat_lv_key) {
            ctx->repeat_lv_key = 0;
        }
        data->continue_reading = (uxQueueMessagesWaiting(ctx->key_queue) > 0);
        return;
    }

    uint32_t rkey = ctx->repeat_lv_key;
    if (rkey != 0) {
        uint32_t now_ms = lv_tick_get();
        if ((now_ms - ctx->repeat_start_ms) >= KEY_REPEAT_DELAY_MS) {
            uint32_t last = ctx->repeat_last_ms;
            if (last == 0 || (now_ms - last) >= KEY_REPEAT_RATE_MS) {
                ctx->repeat_last_ms = now_ms;
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

static void usbHidInputTask(void* arg) {
    auto* ctx = static_cast<UsbHidInputCtx*>(arg);
    LOG_I(TAG, "started");

    while (!lv_is_initialized()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (lock()) {
        ctx->mouse_cursor = lv_image_create(lv_layer_sys());
        lv_obj_remove_flag(ctx->mouse_cursor, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(ctx->mouse_cursor, TT_ASSETS_UI_CURSOR);
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

        unlock();
        LOG_I(TAG, "LVGL input devices registered");
    } else {
        LOG_W(TAG, "could not acquire LVGL lock for indev registration");
    }

    // Drain the HID event queue and route events to the appropriate destinations
    while (ctx->running) {
        UsbHidEvent hid_evt;
        if (xQueueReceive(ctx->hid_queue, &hid_evt, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (!ctx->subscribed) {
                struct Device* hid_dev = device_find_first_active_by_type(&USB_HOST_HID_TYPE);
                if (hid_dev) ctx->subscribed = usb_host_hid_subscribe(hid_dev, ctx->hid_queue);
            }
            continue;
        }

        switch (hid_evt.type) {
        case USB_HID_EVENT_KEY: {
            KeyEvent key_evt = { hid_evt.key.key_code, hid_evt.key.pressed };
            xQueueSend(ctx->key_queue, &key_evt, 0);
            break;
        }
        case USB_HID_EVENT_MOUSE_MOVE: {
            lv_display_t* disp = lv_display_get_default();
            if (!disp) break;
            // Use logical (post-rotation) resolution so clamping matches LVGL's coordinate space
            int32_t w = lv_display_get_horizontal_resolution(disp);
            int32_t h = lv_display_get_vertical_resolution(disp);
            int32_t nx = ctx->mouse_x.load() + hid_evt.mouse_move.dx;
            int32_t ny = ctx->mouse_y.load() + hid_evt.mouse_move.dy;
            if (nx < 0) nx = 0;
            if (nx > w - CURSOR_SIZE - 1) nx = w - CURSOR_SIZE - 1;
            if (ny < 0) ny = 0;
            if (ny > h - CURSOR_SIZE - 1) ny = h - CURSOR_SIZE - 1;
            ctx->mouse_x.store(nx);
            ctx->mouse_y.store(ny);
            break;
        }
        case USB_HID_EVENT_MOUSE_BTN:
            ctx->mouse_btn1.store(hid_evt.mouse_btn.button1);
            break;
        case USB_HID_EVENT_SCROLL: {
            int32_t delta = hid_evt.scroll.delta;
            uint32_t key = (delta < 0) ? USB_HID_KEY_UP : USB_HID_KEY_DOWN;
            int ticks = (delta < 0) ? -delta : delta;
            // Clamp to reasonable maximum to prevent queue overflow
            constexpr int MAX_SCROLL_TICKS = 10;
            if (ticks > MAX_SCROLL_TICKS) ticks = MAX_SCROLL_TICKS;
            for (int t = 0; t < ticks; t++) {
                KeyEvent press   = { key, true  };
                KeyEvent release = { key, false };
                xQueueSend(ctx->key_queue, &press,   0);
                xQueueSend(ctx->key_queue, &release, 0);
            }
            break;
        }
        case USB_HID_EVENT_KEYBOARD_CONNECTED:
            if (ctx->kb_indev && lock(pdMS_TO_TICKS(200))) {
                hardware_keyboard_set_indev(ctx->kb_indev);
                unlock();
            }
            break;
        case USB_HID_EVENT_KEYBOARD_DISCONNECTED:
            if (lock(pdMS_TO_TICKS(200))) {
                hardware_keyboard_set_indev(nullptr);
                unlock();
            }
            break;
        case USB_HID_EVENT_MOUSE_CONNECTED:
            ctx->mouse_connected = true;
            if (ctx->mouse_cursor && lock(pdMS_TO_TICKS(200))) {
                lv_obj_remove_flag(ctx->mouse_cursor, LV_OBJ_FLAG_HIDDEN);
                unlock();
            }
            break;
        case USB_HID_EVENT_MOUSE_DISCONNECTED:
            ctx->mouse_connected = false;
            if (ctx->mouse_cursor && lock(pdMS_TO_TICKS(200))) {
                lv_obj_add_flag(ctx->mouse_cursor, LV_OBJ_FLAG_HIDDEN);
                unlock();
            }
            break;
        default:
            break;
        }
    }

    if (lock()) {
        if (ctx->mouse_indev)  { lv_indev_delete(ctx->mouse_indev);  ctx->mouse_indev  = nullptr; }
        if (ctx->mouse_cursor) { lv_obj_delete(ctx->mouse_cursor);   ctx->mouse_cursor = nullptr; }
        if (ctx->kb_indev) {
            hardware_keyboard_set_indev(nullptr);
            lv_indev_delete(ctx->kb_indev);
            ctx->kb_indev = nullptr;
        }
        unlock();
    }

    LOG_I(TAG, "stopped");
    xSemaphoreGive(ctx->task_done);
    vTaskDelete(nullptr);
}

void startUsbHidInput() {
    if (s_ctx != nullptr) return;

    auto* ctx = new UsbHidInputCtx();

    ctx->hid_queue = xQueueCreate(HID_EVENT_QUEUE_SIZE, sizeof(UsbHidEvent));
    if (!ctx->hid_queue) {
        LOG_E(TAG, "failed to create HID event queue");
        delete ctx;
        return;
    }

    ctx->key_queue = xQueueCreate(KEY_EVENT_QUEUE_SIZE, sizeof(KeyEvent));
    if (!ctx->key_queue) {
        LOG_E(TAG, "failed to create key event queue");
        vQueueDelete(ctx->hid_queue);
        delete ctx;
        return;
    }

    ctx->task_done = xSemaphoreCreateBinary();
    if (!ctx->task_done) {
        LOG_E(TAG, "failed to create task done semaphore");
        vQueueDelete(ctx->hid_queue);
        vQueueDelete(ctx->key_queue);
        delete ctx;
        return;
    }

    struct Device* hid_dev = device_find_first_active_by_type(&USB_HOST_HID_TYPE);
    if (hid_dev) ctx->subscribed = usb_host_hid_subscribe(hid_dev, ctx->hid_queue);

    ctx->running = true;
    if (xTaskCreate(usbHidInputTask, "usb_hid_inp", TASK_STACK, ctx, TASK_PRIORITY, &ctx->task) != pdPASS) {
        LOG_E(TAG, "failed to create task");
        ctx->running = false;
        if (hid_dev) usb_host_hid_unsubscribe(hid_dev, ctx->hid_queue);
        vQueueDelete(ctx->hid_queue);
        vQueueDelete(ctx->key_queue);
        vSemaphoreDelete(ctx->task_done);
        delete ctx;
        return;
    }

    s_ctx = ctx;
    LOG_I(TAG, "started");
}

void stopUsbHidInput() {
    if (!s_ctx) return;
    auto* ctx = s_ctx;
    s_ctx = nullptr;

    ctx->running = false;

    if (xSemaphoreTake(ctx->task_done, pdMS_TO_TICKS(STOP_TIMEOUT_MS)) != pdTRUE) {
        LOG_W(TAG, "task stop timed out, force terminating");
        vTaskDelete(ctx->task);
        // Task was killed before it could clean up LVGL objects; do it here to
        // prevent mouse_read_cb / keyboard_read_cb from running with a freed ctx.
        if (lock(pdMS_TO_TICKS(200))) {
            if (ctx->mouse_indev)  { lv_indev_delete(ctx->mouse_indev);  ctx->mouse_indev  = nullptr; }
            if (ctx->mouse_cursor) { lv_obj_delete(ctx->mouse_cursor);   ctx->mouse_cursor = nullptr; }
            if (ctx->kb_indev) {
                hardware_keyboard_set_indev(nullptr);
                lv_indev_delete(ctx->kb_indev);
                ctx->kb_indev = nullptr;
            }
            unlock();
        }
    }
    ctx->task = nullptr;

    if (ctx->subscribed) {
        struct Device* hid_dev = device_find_first_active_by_type(&USB_HOST_HID_TYPE);
        if (hid_dev) usb_host_hid_unsubscribe(hid_dev, ctx->hid_queue);
    }
    vQueueDelete(ctx->hid_queue);
    vQueueDelete(ctx->key_queue);
    vSemaphoreDelete(ctx->task_done);
    delete ctx;

    LOG_I(TAG, "stopped");
}

} // namespace tt::lvgl

#endif // ESP_PLATFORM
