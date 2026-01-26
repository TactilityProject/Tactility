#ifdef ESP_PLATFORM

#include <Tactility/app/crashdiagnostics/QrHelpers.h>
#include <Tactility/app/crashdiagnostics/QrUrl.h>
#include <Tactility/app/launcher/Launcher.h>
#include <Tactility/hal/Device.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/service/loader/Loader.h>

#include <lvgl.h>
#include <qrcode.h>

namespace tt::app::crashdiagnostics {

static const auto LOGGER = Logger("CrashDiagnostics");

extern const AppManifest manifest;

void onContinuePressed(lv_event_t* event) {
    stop(manifest.appId);
    launcher::start();
}

class CrashDiagnosticsApp : public App {

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        auto* display = lv_obj_get_display(parent);
        int32_t parent_height = lv_display_get_vertical_resolution(display) - lvgl::STATUSBAR_HEIGHT;

        lv_obj_add_event_cb(parent, onContinuePressed, LV_EVENT_SHORT_CLICKED, nullptr);
        auto* top_label = lv_label_create(parent);
        lv_label_set_text(top_label, "Oops! We've crashed ..."); // TODO: Funny messages
        lv_obj_align(top_label, LV_ALIGN_TOP_MID, 0, 2);

        auto* bottom_label = lv_label_create(parent);
        if (hal::hasDevice(hal::Device::Type::Touch)) {
            lv_label_set_text(bottom_label, "Tap screen to continue");
        } else {
            lv_label_set_text(bottom_label, "Reboot device to continue");
        }
        lv_obj_align(bottom_label, LV_ALIGN_BOTTOM_MID, 0, -2);

        std::string url = getUrlFromCrashData();
        LOGGER.info("{}", url);
        size_t url_length = url.length();

        int qr_version;
        if (!getQrVersionForBinaryDataLength(url_length, qr_version)) {
            LOGGER.error("QR is too large");
            stop(manifest.appId);
            return;
        }

        LOGGER.info("QR version {} (length: {})", qr_version, url_length);
        auto qrcodeData = std::make_shared<uint8_t[]>(qrcode_getBufferSize(qr_version));
        if (qrcodeData == nullptr) {
            LOGGER.error("Failed to allocate QR buffer");
            stop(manifest.appId);
            return;
        }

        QRCode qrcode;
        LOGGER.info("QR init text");
        if (qrcode_initText(&qrcode, qrcodeData.get(), qr_version, ECC_LOW, url.c_str()) != 0) {
            LOGGER.error("QR init text failed");
            stop(manifest.appId);
            return;
        }

        LOGGER.info("QR size: {}", qrcode.size);

        // Calculate QR dot size
        int32_t top_label_height = lv_obj_get_height(top_label) + 2;
        int32_t bottom_label_height = lv_obj_get_height(bottom_label) + 2;
        LOGGER.info("Create canvas");
        int32_t available_height = parent_height - top_label_height - bottom_label_height;
        int32_t available_width = lv_display_get_horizontal_resolution(display);
        int32_t smallest_size = std::min(available_height, available_width);
        int32_t pixel_size;
        if (qrcode.size * 2 <= smallest_size) {
            pixel_size = 2;
        } else if (qrcode.size <= smallest_size) {
            pixel_size = 1;
        } else {
            LOGGER.error("QR code won't fit screen");
            stop(manifest.appId);
            return;
        }

        auto* canvas = lv_canvas_create(parent);
        lv_obj_set_size(canvas, pixel_size * qrcode.size, pixel_size * qrcode.size);
        lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_set_content_height(canvas, qrcode.size * pixel_size);
        lv_obj_set_content_width(canvas, qrcode.size * pixel_size);

        LOGGER.info("Create draw buffer");
        auto* draw_buf = lv_draw_buf_create(pixel_size * qrcode.size, pixel_size * qrcode.size, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (draw_buf == nullptr) {
            LOGGER.error("Failed to allocate draw buffer");
            stop(manifest.appId);
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
};

extern const AppManifest manifest = {
    .appId = "CrashDiagnostics",
    .appName = "Crash Diagnostics",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<CrashDiagnosticsApp>
};

void start() {
    app::start(manifest.appId);
}

} // namespace

#endif