#ifdef ESP_PLATFORM

#include <Tactility/app/crashdiagnostics/QrHelpers.h>
#include <Tactility/app/crashdiagnostics/QrUrl.h>
#include <Tactility/app/launcher/Launcher.h>
#include <Tactility/lvgl/Statusbar.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <qrcode.h>
#include <tactility/drivers/pointer.h>
#include <tactility/log.h>

#include <memory>

namespace tt::app::crashdiagnostics {

constexpr auto* TAG = "CrashDiagnostics";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    // Set when widget creation hit an unrecoverable error (e.g. the QR code doesn't fit on
    // screen) - appMain() skips the event loop and closes immediately without ever starting
    // the launcher, matching the old model's stop()-without-launcher-start() error paths.
    bool hasFatalError = false;
    // Set by onContinuePressed() right before it emits APP_EVENT_CLOSE - read by appMain()
    // after its own thread finishes cleanup, to decide whether to start the launcher
    // afterwards (matches the old model's onContinuePressed(): stop() then launcher::start()).
    bool continuePressed = false;
};


void onContinuePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->continuePressed = true;
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself. launcher::start() is deferred to appMain(), after this app's
    // own thread has finished cleaning up.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    auto* display = lv_obj_get_display(parent);
    int32_t parent_height = lv_display_get_vertical_resolution(display) - lvgl::statusbar_get_height();

    lv_obj_add_event_cb(parent, onContinuePressed, LV_EVENT_SHORT_CLICKED, ctx);
    auto* top_label = lv_label_create(parent);
    lv_label_set_text(top_label, "Oops! We've crashed ..."); // TODO: Funny messages
    lv_obj_align(top_label, LV_ALIGN_TOP_MID, 0, 2);

    auto* bottom_label = lv_label_create(parent);
    if (device_has_active_by_type(&POINTER_TYPE)) {
        lv_label_set_text(bottom_label, "Tap screen to continue");
    } else {
        lv_label_set_text(bottom_label, "Reboot device to continue");
    }
    lv_obj_align(bottom_label, LV_ALIGN_BOTTOM_MID, 0, -2);

    std::string url = getUrlFromCrashData();
    LOG_I(TAG, "%s", url.c_str());
    size_t url_length = url.length();

    int qr_version;
    if (!getQrVersionForBinaryDataLength(url_length, qr_version)) {
        LOG_E(TAG, "QR is too large");
        ctx->hasFatalError = true;
        return;
    }

    LOG_I(TAG, "QR version %d (length: %d)", qr_version, (int)url_length);
    auto qrcodeData = std::make_shared<uint8_t[]>(qrcode_getBufferSize(qr_version));
    if (qrcodeData == nullptr) {
        LOG_E(TAG, "Failed to allocate QR buffer");
        ctx->hasFatalError = true;
        return;
    }

    QRCode qrcode;
    LOG_I(TAG, "QR init text");
    if (qrcode_initText(&qrcode, qrcodeData.get(), qr_version, ECC_LOW, url.c_str()) != 0) {
        LOG_E(TAG, "QR init text failed");
        ctx->hasFatalError = true;
        return;
    }

    LOG_I(TAG, "QR size: %d", qrcode.size);

    // Calculate QR dot size
    int32_t top_label_height = lv_obj_get_height(top_label) + 2;
    int32_t bottom_label_height = lv_obj_get_height(bottom_label) + 2;
    LOG_I(TAG, "Create canvas");
    int32_t available_height = parent_height - top_label_height - bottom_label_height;
    int32_t available_width = lv_display_get_horizontal_resolution(display);
    int32_t smallest_size = std::min(available_height, available_width);
    int32_t pixel_size;
    if (qrcode.size * 2 <= smallest_size) {
        pixel_size = 2;
    } else if (qrcode.size <= smallest_size) {
        pixel_size = 1;
    } else {
        LOG_E(TAG, "QR code won't fit screen");
        ctx->hasFatalError = true;
        return;
    }

    auto* canvas = lv_canvas_create(parent);
    lv_obj_set_size(canvas, pixel_size * qrcode.size, pixel_size * qrcode.size);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_content_height(canvas, qrcode.size * pixel_size);
    lv_obj_set_content_width(canvas, qrcode.size * pixel_size);

    LOG_I(TAG, "Create draw buffer");
    auto* draw_buf = lv_draw_buf_create(pixel_size * qrcode.size, pixel_size * qrcode.size, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (draw_buf == nullptr) {
        LOG_E(TAG, "Failed to allocate draw buffer");
        ctx->hasFatalError = true;
        return;
    }

    lv_canvas_set_draw_buf(canvas, draw_buf);

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            bool colored = qrcode_getModule(&qrcode, x, y);
            auto color = colored ? lv_color_white() : lv_color_black();
            int32_t pos_x = x * pixel_size;
            int32_t pos_y = y * pixel_size;
            for (int px = 0; px < pixel_size; px++) {
                for (int py = 0; py < pixel_size; py++) {
                    lv_canvas_set_px(canvas, pos_x + px, pos_y + py, color, LV_OPA_COVER);
                }
            }
        }
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    if (!ctx.hasFatalError) {
        bool shouldClose = false;
        while (!shouldClose) {
            AppEvent event {};
            if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
                break;
            }
            switch (event.type) {
            case APP_EVENT_CLOSE:
                    app_manager_finish(appInstanceId);
                    shouldClose = true;
                    break;
                default:
                    break;
            }
        }
    } else {
        app_manager_finish(appInstanceId);
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    bool continuePressed = ctx.continuePressed;

    if (continuePressed) {
        launcher::start();
    }

    return 0;
}

} // namespace

void start() {
    uint32_t instanceId = 0;
    app_manager_start(manifest.id, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "CrashDiagnostics",
    .name = "Crash Diagnostics",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace

#endif
