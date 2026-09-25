#include <Tactility/Tactility.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/paths.h>
#include <app/scheduler.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>
#include <lvgl_window_manager/window_manager.h>

#include <sys/stat.h>
#include <unistd.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/camera.h>
#include <tactility/memory.h>

namespace tt::app::camera {

extern const ::AppManifest manifest;

constexpr uint32_t FRAME_TIMEOUT_MS = 200;

namespace {

struct Context {
    uint32_t appInstanceId;

    CameraHandle camHandle = nullptr;

    uint8_t* displayBuf[2] = {};
    int displayBufIdx = 0;
    int publishedDisplayBufIdx = -1;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;

    TaskHandle_t camTask = nullptr;
    SemaphoreHandle_t stopSem = nullptr;
    SemaphoreHandle_t doneSem = nullptr;

    SemaphoreHandle_t camHandleMutex = nullptr;

    std::atomic<bool> captureNext {false};
    std::atomic<bool> stopping {false};

    lv_display_rotation_t displayRotation = LV_DISPLAY_ROTATION_0;

    lv_obj_t* previewContainer = nullptr;
    lv_obj_t* previewCanvas = nullptr;
    lv_obj_t* toastLabel = nullptr;
    lv_timer_t* toastTimer = nullptr;
};

static void anim_opa_cb(void* var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
}

static void toast_fade_out_ready_cb(lv_anim_t* a) {
    lv_obj_t* toast = (lv_obj_t*)a->var;
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
}

static void toastTimerCb(lv_timer_t *timer) {
    auto* ctx = static_cast<Context*>(lv_timer_get_user_data(timer));
    lv_timer_delete(timer);
    if (ctx != nullptr) {
        ctx->toastTimer = nullptr;
        if (ctx->toastLabel != nullptr) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, ctx->toastLabel);
            lv_anim_set_values(&a, LV_OPA_90, LV_OPA_TRANSP);
            lv_anim_set_time(&a, 300);
            lv_anim_set_exec_cb(&a, anim_opa_cb);
            lv_anim_set_completed_cb(&a, toast_fade_out_ready_cb);
            lv_anim_start(&a);
        }
    }
}

static void showToast(Context* ctx, const char *text) {
    if (ctx == nullptr || ctx->toastLabel == nullptr) {
        return;
    }
    if (!lvgl_try_lock(pdMS_TO_TICKS(100))) return;

    lv_anim_delete(ctx->toastLabel, anim_opa_cb);

    lv_label_set_text(ctx->toastLabel, text);
    lv_obj_remove_flag(ctx->toastLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->toastLabel);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ctx->toastLabel);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_90);
    lv_anim_set_time(&a, 250);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    if (ctx->toastTimer != nullptr) {
        lv_timer_del(ctx->toastTimer);
    }
    ctx->toastTimer = lv_timer_create(toastTimerCb, 3000, ctx);
    lvgl_unlock();
}

void buildTimestampedPath(const char* suffix, char* out, size_t outLen) {
    char dir[200];
    if (app_paths_get_user_data_directory("tactility.camera", dir, sizeof(dir)) != ERROR_NONE) {
        LOG_E("Camera", "Failed to resolve user data directory");
        out[0] = '\0';
        return;
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        LOG_E("Camera", "mkdir failed for %s: errno=%d", dir, errno);
    }

    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    static uint32_t sequence = 0;
    char fname[64];
    strftime(fname, sizeof(fname), "/%Y%m%d_%H%M%S", &tm_info);
    snprintf(out, outLen, "%s%s_%04u%s", dir, fname, static_cast<unsigned>(sequence++), suffix);
}

bool saveJpeg(Context* ctx) {
    if (!ctx->camHandle) {
        showToast(ctx, "Capture failed: driver unavailable");
        return false;
    }

    char path[256];
    buildTimestampedPath(".jpg", path, sizeof(path));

    uint8_t* jpegBuf = nullptr;
    size_t   jpegLen = 0;
    if (camera_capture_jpeg(ctx->camHandle, &jpegBuf, &jpegLen, 100) != ERROR_NONE || !jpegBuf) {
        showToast(ctx, "Capture failed: encode error");
        return false;
    }

    bool ok = false;
    {
        FILE* f = fopen(path, "wbx");
        if (f) {
            const size_t written = fwrite(jpegBuf, 1, jpegLen, f);
            const int closeResult = fclose(f);
            ok = written == jpegLen && closeResult == 0;
            if (!ok) {
                unlink(path);
            }
        } else {
            LOG_E("Camera", "fopen failed for %s: errno=%d", path, errno);
        }
    }
    heap_caps_free(jpegBuf);

    if (ok) {
        LOG_I("Camera", "Saved JPEG: %s (%zu bytes)", path, jpegLen);
        const char* basename = strrchr(path, '/');
        char msg[sizeof(path) + 8];
        snprintf(msg, sizeof(msg), "Saved: %s", basename ? basename + 1 : path);
        showToast(ctx, msg);
    } else {
        showToast(ctx, "Capture failed: cannot write file");
    }
    return ok;
}

static void syncRotation(Context* ctx) {
    if (!ctx->camHandleMutex) return;
    xSemaphoreTake(ctx->camHandleMutex, portMAX_DELAY);

    if (!ctx->camHandle) {
        xSemaphoreGive(ctx->camHandleMutex);
        return;
    }

    CameraRotation rot;
    switch (ctx->displayRotation) {
        case LV_DISPLAY_ROTATION_90:  rot = CAMERA_ROTATION_90;  break;
        case LV_DISPLAY_ROTATION_180: rot = CAMERA_ROTATION_180; break;
        case LV_DISPLAY_ROTATION_270: rot = CAMERA_ROTATION_270; break;
        default:                      rot = CAMERA_ROTATION_0;   break;
    }
    camera_set_rotation(ctx->camHandle, rot);

    ctx->frameWidth  = camera_get_width(ctx->camHandle);
    ctx->frameHeight = camera_get_height(ctx->camHandle);

    xSemaphoreGive(ctx->camHandleMutex);
}

void onDisplayRotation(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    if (!ctx) return;
    // Called on LVGL task - safe to read display rotation here
    lv_display_t* disp = static_cast<lv_display_t*>(lv_event_get_target(e));
    if (disp) ctx->displayRotation = lv_display_get_rotation(disp);
    syncRotation(ctx);
}

void onCaptureBtn(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    if (ctx) {
        ctx->captureNext.store(true);
        showToast(ctx, "Capturing...");
    }
}

void cameraTaskEntry(void* arg) {
    auto* ctx = static_cast<Context*>(arg);

    Device* camDev = nullptr;
    if (device_get_first_active_by_type(&CAMERA_TYPE, &camDev) != ERROR_NONE) {
        LOG_E("Camera", "Camera device not found");
        xSemaphoreGive(ctx->doneSem);
        vTaskDelete(nullptr);
        return;
    }

    CameraHandle handle = nullptr;
    if (camera_open(camDev, &handle) != ERROR_NONE) {
        LOG_E("Camera", "camera_open failed");
        device_put(camDev);
        xSemaphoreGive(ctx->doneSem);
        vTaskDelete(nullptr);
        return;
    }
    device_put(camDev);

    xSemaphoreTake(ctx->camHandleMutex, portMAX_DELAY);
    ctx->camHandle = handle;
    xSemaphoreGive(ctx->camHandleMutex);
    syncRotation(ctx);
    showToast(ctx, "Camera Started...");

    size_t frameSz = (size_t)camera_get_width(handle) * camera_get_height(handle) * 2;
    for (int i = 0; i < 2; i++) {
        const MemoryPolicy policy { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
        ctx->displayBuf[i] = static_cast<uint8_t*>(memory_alloc_with_policy(frameSz, &policy));
        if (!ctx->displayBuf[i]) {
            LOG_E("Camera", "Failed to alloc display buffer %d", i);
            xSemaphoreTake(ctx->camHandleMutex, portMAX_DELAY);
            camera_close(handle);
            ctx->camHandle = nullptr;
            xSemaphoreGive(ctx->camHandleMutex);
            for (int j = 0; j < 2; j++) {
                heap_caps_free(ctx->displayBuf[j]);
                ctx->displayBuf[j] = nullptr;
            }
            xSemaphoreGive(ctx->doneSem);
            vTaskDelete(nullptr);
            return;
        }
    }

    uint8_t* frameBuf = nullptr;
    size_t frameLen = 0;

    while (true) {
        if (xSemaphoreTake(ctx->stopSem, 0) == pdTRUE) break;

        if (ctx->captureNext.load()) {
            ctx->captureNext.store(false);
            saveJpeg(ctx);
            continue;
        }

        uint32_t frameWidth = 0;
        uint32_t frameHeight = 0;
        error_t err = camera_get_frame(handle, &frameBuf, &frameLen, FRAME_TIMEOUT_MS, &frameWidth, &frameHeight);
        if (err == ERROR_TIMEOUT) continue;
        if (err != ERROR_NONE) {
            LOG_E("Camera", "get_frame error: %d", (int)err);
            break;
        }

        int idx = ctx->publishedDisplayBufIdx < 0 ? 0 : ctx->publishedDisplayBufIdx ^ 1;
        if (frameLen > frameSz || (size_t)frameWidth * frameHeight * 2 > frameSz) {
            LOG_E("Camera", "Frame too large: %zu bytes (%ux%u)", frameLen, (unsigned)frameWidth, (unsigned)frameHeight);
            camera_release_frame(handle);
            continue;
        }
        memcpy(ctx->displayBuf[idx], frameBuf, frameLen);
        camera_release_frame(handle);
        ctx->displayBufIdx = idx;

        if (lvgl_try_lock(pdMS_TO_TICKS(50))) {
            lv_obj_t* previewCanvas = ctx->previewCanvas;
            const bool stopping = ctx->stopping.load();
            auto w = static_cast<int32_t>(frameWidth);
            auto h = static_cast<int32_t>(frameHeight);
            if (previewCanvas != nullptr && !stopping) {
                lv_canvas_set_buffer(previewCanvas, ctx->displayBuf[idx], w, h, LV_COLOR_FORMAT_RGB565);
                ctx->publishedDisplayBufIdx = idx;
            }
            lvgl_unlock();
        }
    }

    xSemaphoreTake(ctx->camHandleMutex, portMAX_DELAY);
    camera_close(handle);
    ctx->camHandle = nullptr;
    xSemaphoreGive(ctx->camHandleMutex);

    for (int i = 0; i < 2; i++) {
        heap_caps_free(ctx->displayBuf[i]);
        ctx->displayBuf[i] = nullptr;
    }

    xSemaphoreGive(ctx->doneSem);
    vTaskDelete(nullptr);
}

void startCamera(Context* ctx) {
    ctx->stopSem = xSemaphoreCreateBinary();
    ctx->doneSem = xSemaphoreCreateBinary();
    ctx->camHandleMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(cameraTaskEntry, "cam", 8 * 1024, ctx, 5, &ctx->camTask, 0);
}

void stopCamera(Context* ctx) {
    if (!ctx->camTask) return;

    xSemaphoreGive(ctx->stopSem);
    while (xSemaphoreTake(ctx->doneSem, portMAX_DELAY) != pdTRUE) {
    }
    ctx->camTask = nullptr;

    vSemaphoreDelete(ctx->stopSem); ctx->stopSem = nullptr;
    vSemaphoreDelete(ctx->doneSem); ctx->doneSem = nullptr;
    vSemaphoreDelete(ctx->camHandleMutex); ctx->camHandleMutex = nullptr;
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->stopping.store(false);

    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Camera");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lvgl_toolbar_add_text_button_action(toolbar, LV_SYMBOL_EYE_OPEN, onCaptureBtn, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(main_wrapper, 1);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_height(main_wrapper, LV_PCT(100));
    lv_obj_set_style_pad_all(main_wrapper, 6, 0);
    lv_obj_set_style_border_width(main_wrapper, 0, 0);
    lv_obj_remove_flag(main_wrapper, LV_OBJ_FLAG_SCROLLABLE);

    ctx->previewContainer = lv_obj_create(main_wrapper);
    lv_obj_set_size(ctx->previewContainer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ctx->previewContainer, lv_color_black(), 0);
    lv_obj_set_style_border_width(ctx->previewContainer, 0, 0);
    lv_obj_set_style_pad_all(ctx->previewContainer, 0, 0);
    lv_obj_set_style_radius(ctx->previewContainer, 0, 0);
    lv_obj_remove_flag(ctx->previewContainer, LV_OBJ_FLAG_SCROLLABLE);

    ctx->previewCanvas = lv_canvas_create(ctx->previewContainer);
    lv_obj_set_style_bg_color(ctx->previewCanvas, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->previewCanvas, LV_OPA_COVER, 0);
    lv_obj_center(ctx->previewCanvas);

    ctx->toastLabel = lv_label_create(lv_layer_top());
    lv_obj_align(ctx->toastLabel, LV_ALIGN_TOP_MID, 0, 52 * 2 + 24);
    lv_obj_set_style_radius(ctx->toastLabel, 16, 0);
    lv_obj_set_style_pad_hor(ctx->toastLabel, 24, 0);
    lv_obj_set_style_pad_ver(ctx->toastLabel, 12, 0);
    lv_obj_set_style_border_width(ctx->toastLabel, 2, 0);
    lv_obj_set_style_text_font(ctx->toastLabel, lvgl_get_text_font(FONT_SIZE_DEFAULT), 0);
    lv_obj_set_style_bg_opa(ctx->toastLabel, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(ctx->toastLabel, lv_color_hex(0x181825), 0);
    lv_obj_set_style_text_color(ctx->toastLabel, lv_color_white(), 0);
    lv_obj_set_style_border_color(ctx->toastLabel, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_shadow_width(ctx->toastLabel, 16, 0);
    lv_obj_set_style_shadow_color(ctx->toastLabel, lv_color_black(), 0);
    lv_obj_add_flag(ctx->toastLabel, LV_OBJ_FLAG_HIDDEN);

    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        ctx->displayRotation = lv_display_get_rotation(disp);
        lv_display_add_event_cb(disp, onDisplayRotation, LV_EVENT_RESOLUTION_CHANGED, ctx);
    }

    startCamera(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_display_t* disp = lv_display_get_default();
    if (disp) lv_display_remove_event_cb_with_user_data(disp, onDisplayRotation, ctx);

    if (ctx->toastTimer != nullptr) {
        lv_timer_delete(ctx->toastTimer);
        ctx->toastTimer = nullptr;
    }

    if (ctx->toastLabel != nullptr) {
        lv_anim_delete(ctx->toastLabel, nullptr);
    }

    ctx->stopping.store(true);

    if (ctx->previewCanvas != nullptr) {
        lv_obj_delete(ctx->previewCanvas);
    }
    ctx->previewCanvas = nullptr;

    stopCamera(ctx);

    ctx->previewContainer = nullptr;
    if (ctx->toastLabel != nullptr) {
        lv_obj_delete(ctx->toastLabel);
        ctx->toastLabel = nullptr;
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.camera",
    .name = "Camera",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {.depth = 4096, .desired_memory_capability = MEMORY_CAPABILITY_EXTERNAL }
};

} // namespace tt::app::camera
