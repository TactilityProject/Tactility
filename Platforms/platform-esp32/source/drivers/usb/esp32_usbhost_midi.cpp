#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/usb_host_midi.h>
#include <tactility/log.h>

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/portmacro.h>

#include <usb/usb_host.h>
#include <usb/usb_helpers.h>
#include <usb/usb_types_ch9.h>
#include <usb/usb_types_stack.h>

#define TAG "esp32_usbhost_midi"

constexpr auto MIDI_TASK_STACK        = 4096;
constexpr auto MIDI_TASK_PRIORITY     = 5;
constexpr auto MIDI_STOP_TIMEOUT_MS   = 2000;
constexpr auto MIDI_TRANSFER_BUF_SIZE = 512;

constexpr uint8_t MIDI_INTF_CLASS    = 0x01;
constexpr uint8_t MIDI_INTF_SUBCLASS = 0x03;

struct UsbMidiCtx {
    usb_host_client_handle_t client_hdl   = nullptr;
    usb_device_handle_t      dev_hdl      = nullptr;
    usb_transfer_t*          transfer     = nullptr;
    uint8_t                  ep_addr      = 0;
    uint8_t                  intf_num     = 0;
    std::atomic<bool>        connected{false};
    std::atomic<bool>        running{false};
    TaskHandle_t             task_handle  = nullptr;
    SemaphoreHandle_t        task_done    = nullptr;

    portMUX_TYPE          callback_lock = portMUX_INITIALIZER_UNLOCKED;
    usb_midi_message_cb_t callback      = nullptr;
    void*                 callback_arg  = nullptr;
};

static bool find_midi_interface(const usb_config_desc_t* cfg, uint8_t* out_intf, uint8_t* out_ep) {
    int offset = 0;
    const usb_standard_desc_t* cur = reinterpret_cast<const usb_standard_desc_t*>(cfg);

    while ((cur = usb_parse_next_descriptor_of_type(
                cur, cfg->wTotalLength, USB_W_VALUE_DT_INTERFACE, &offset)) != nullptr) {
        const auto* intf = reinterpret_cast<const usb_intf_desc_t*>(cur);
        if (intf->bInterfaceClass != MIDI_INTF_CLASS || intf->bInterfaceSubClass != MIDI_INTF_SUBCLASS) continue;

        int ep_offset = offset;
        const usb_standard_desc_t* ep_cur = cur;
        for (int e = 0; e < intf->bNumEndpoints; e++) {
            ep_cur = usb_parse_next_descriptor_of_type(ep_cur, cfg->wTotalLength, USB_W_VALUE_DT_ENDPOINT, &ep_offset);
            if (!ep_cur) break;
            const auto* ep = reinterpret_cast<const usb_ep_desc_t*>(ep_cur);
            usb_transfer_type_t xtype = USB_EP_DESC_GET_XFERTYPE(ep);
            bool is_in = USB_EP_DESC_GET_EP_DIR(ep) == 1;
            if (is_in && (xtype == USB_TRANSFER_TYPE_BULK || xtype == USB_TRANSFER_TYPE_INTR)) {
                *out_intf = intf->bInterfaceNumber;
                *out_ep   = ep->bEndpointAddress;
                return true;
            }
        }
    }
    return false;
}

static void dispatch_midi_packets(UsbMidiCtx* ctx, const uint8_t* buf, int len) {
    usb_midi_message_cb_t cb;
    void* arg;
    taskENTER_CRITICAL(&ctx->callback_lock);
    cb  = ctx->callback;
    arg = ctx->callback_arg;
    taskEXIT_CRITICAL(&ctx->callback_lock);
    if (!cb) return;

    for (int i = 0; i + 3 < len; i += 4) {
        uint8_t cin = buf[i] & 0x0F;
        if (cin < 0x02) continue;
        usb_midi_message_t msg = {
            .cable  = static_cast<uint8_t>(buf[i] >> 4),
            .status = buf[i + 1],
            .data1  = buf[i + 2],
            .data2  = buf[i + 3],
        };
        cb(&msg, arg);
    }
}

static void midi_transfer_cb(usb_transfer_t* transfer) {
    auto* ctx = static_cast<UsbMidiCtx*>(transfer->context);
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
        dispatch_midi_packets(ctx, transfer->data_buffer, transfer->actual_num_bytes);
    }
    if (ctx->running && ctx->connected && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        transfer->num_bytes = MIDI_TRANSFER_BUF_SIZE;
        if (usb_host_transfer_submit(transfer) != ESP_OK) {
            LOG_E(TAG, "failed to resubmit MIDI transfer");
        }
    }
}

static void client_event_cb(const usb_host_client_event_msg_t* msg, void* arg) {
    auto* ctx = static_cast<UsbMidiCtx*>(arg);

    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        uint8_t addr = msg->new_dev.address;
        usb_device_handle_t dev_hdl = nullptr;
        if (usb_host_device_open(ctx->client_hdl, addr, &dev_hdl) != ESP_OK) return;

        const usb_config_desc_t* cfg = nullptr;
        if (usb_host_get_active_config_descriptor(dev_hdl, &cfg) != ESP_OK) {
            usb_host_device_close(ctx->client_hdl, dev_hdl);
            return;
        }

        uint8_t intf_num = 0, ep_addr = 0;
        if (!find_midi_interface(cfg, &intf_num, &ep_addr)) {
            LOG_D(TAG, "USB device addr=%d: no MIDI Streaming interface found", addr);
            usb_host_device_close(ctx->client_hdl, dev_hdl);
            return;
        }

        if (usb_host_interface_claim(ctx->client_hdl, dev_hdl, intf_num, 0) != ESP_OK) {
            LOG_E(TAG, "failed to claim MIDI interface %d", intf_num);
            usb_host_device_close(ctx->client_hdl, dev_hdl);
            return;
        }

        ctx->dev_hdl  = dev_hdl;
        ctx->intf_num = intf_num;
        ctx->ep_addr  = ep_addr;

        ctx->transfer->device_handle    = dev_hdl;
        ctx->transfer->bEndpointAddress = ep_addr;
        ctx->transfer->num_bytes        = MIDI_TRANSFER_BUF_SIZE;
        ctx->transfer->callback         = midi_transfer_cb;
        ctx->transfer->context          = ctx;
        ctx->transfer->timeout_ms       = 0;

        if (usb_host_transfer_submit(ctx->transfer) == ESP_OK) {
            ctx->connected = true;
            LOG_I(TAG, "MIDI device connected (intf=%d ep=0x%02x)", intf_num, ep_addr);
        } else {
            LOG_E(TAG, "failed to submit initial MIDI transfer");
            usb_host_interface_release(ctx->client_hdl, dev_hdl, intf_num);
            usb_host_device_close(ctx->client_hdl, dev_hdl);
            ctx->dev_hdl = nullptr;
        }

    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (ctx->dev_hdl && msg->dev_gone.dev_hdl == ctx->dev_hdl) {
            LOG_I(TAG, "MIDI device disconnected");
            ctx->connected = false;
            usb_host_interface_release(ctx->client_hdl, ctx->dev_hdl, ctx->intf_num);
            usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
            ctx->dev_hdl = nullptr;
        }
    }
}

static void midiClientTask(void* arg) {
    auto* ctx = static_cast<UsbMidiCtx*>(arg);
    LOG_I(TAG, "MIDI client task started");

    while (ctx->running) {
        usb_host_client_handle_events(ctx->client_hdl, pdMS_TO_TICKS(100));
    }

    if (ctx->dev_hdl) {
        ctx->connected = false;
        usb_host_interface_release(ctx->client_hdl, ctx->dev_hdl, ctx->intf_num);
        usb_host_device_close(ctx->client_hdl, ctx->dev_hdl);
        ctx->dev_hdl = nullptr;
    }

    LOG_I(TAG, "MIDI client task stopped");
    xSemaphoreGive(ctx->task_done);
    vTaskDelete(nullptr);
}

static void api_set_callback(struct Device* device, usb_midi_message_cb_t callback, void* user_data) {
    auto* ctx = static_cast<UsbMidiCtx*>(device_get_driver_data(device));
    if (!ctx) return;
    taskENTER_CRITICAL(&ctx->callback_lock);
    ctx->callback     = callback;
    ctx->callback_arg = user_data;
    taskEXIT_CRITICAL(&ctx->callback_lock);
}

static bool api_is_connected(struct Device* device) {
    auto* ctx = static_cast<UsbMidiCtx*>(device_get_driver_data(device));
    return ctx && ctx->connected.load();
}

static const UsbMidiApi midi_api = {
    .set_callback = api_set_callback,
    .is_connected = api_is_connected,
};

extern "C" {

static error_t start_device(struct Device* device) {
    auto* ctx = new UsbMidiCtx();

    if (usb_host_transfer_alloc(MIDI_TRANSFER_BUF_SIZE, 0, &ctx->transfer) != ESP_OK) {
        LOG_E(TAG, "failed to allocate MIDI transfer");
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->task_done = xSemaphoreCreateBinary();
    if (!ctx->task_done) {
        LOG_E(TAG, "failed to create task done semaphore");
        usb_host_transfer_free(ctx->transfer);
        delete ctx;
        return ERROR_RESOURCE;
    }

    const usb_host_client_config_t client_cfg = {
        .is_synchronous    = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg          = ctx,
        },
    };
    if (usb_host_client_register(&client_cfg, &ctx->client_hdl) != ESP_OK) {
        LOG_E(TAG, "failed to register USB host client");
        vSemaphoreDelete(ctx->task_done);
        usb_host_transfer_free(ctx->transfer);
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->running = true;
    BaseType_t result = xTaskCreate(midiClientTask, "midi_client", MIDI_TASK_STACK,
                                    ctx, MIDI_TASK_PRIORITY, &ctx->task_handle);
    if (result != pdPASS) {
        LOG_E(TAG, "failed to create midi_client task");
        ctx->running = false;
        usb_host_client_deregister(ctx->client_hdl);
        vSemaphoreDelete(ctx->task_done);
        usb_host_transfer_free(ctx->transfer);
        delete ctx;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, ctx);
    LOG_I(TAG, "started");
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbMidiCtx*>(device_get_driver_data(device));
    if (!ctx) return ERROR_NONE;

    ctx->running = false;
    usb_host_client_unblock(ctx->client_hdl);

    if (xSemaphoreTake(ctx->task_done, pdMS_TO_TICKS(MIDI_STOP_TIMEOUT_MS)) != pdTRUE) {
        LOG_E(TAG, "MIDI client task stop timed out after %dms — a full USB host restart may be required", MIDI_STOP_TIMEOUT_MS);
        if (ctx->dev_hdl) {
            ctx->connected = false;
            if (usb_host_interface_release(ctx->client_hdl, ctx->dev_hdl, ctx->intf_num) != ESP_OK) {
                LOG_W(TAG, "failed to release MIDI interface during force-stop");
            }
            if (usb_host_device_close(ctx->client_hdl, ctx->dev_hdl) != ESP_OK) {
                LOG_W(TAG, "failed to close MIDI device during force-stop");
            }
            ctx->dev_hdl = nullptr;
        }
        vTaskDelete(ctx->task_handle);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ctx->task_handle = nullptr;
    vSemaphoreDelete(ctx->task_done);

    usb_host_client_deregister(ctx->client_hdl);
    ctx->client_hdl = nullptr;

    usb_host_transfer_free(ctx->transfer);
    ctx->transfer = nullptr;

    device_set_driver_data(device, nullptr);
    delete ctx;
    LOG_I(TAG, "stopped");
    return ERROR_NONE;
}

Driver esp32_usbhost_midi_driver = {
    .name         = "esp32_usbhost_midi",
    .compatible   = (const char*[]) { "espressif,esp32-usbhost-midi", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = &midi_api,
    .device_type  = &USB_HOST_MIDI_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"
