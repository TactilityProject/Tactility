// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For the lv_obj_t and lv_obj_class_t declarations

#include <lvgl/widgets/qr.h>

#include <tactility/memory.h>

extern "C" {
#include <qrcode.h>
}

#include <algorithm>
#include <cstring>

// Bounds the stack that qrcode_initBytes() uses: its buffers are sized by the QR version
#define QR_MAX_VERSION 25
// The code needs this many modules of empty space on every side
#define QR_QUIET_ZONE_MODULES 4

// Bytes that fit in each version (starting at version 1) with low error correction.
// The QR library doesn't check the length, so the version has to be big enough before encoding.
static const uint16_t QR_VERSION_BYTE_CAPACITY[QR_MAX_VERSION] = {
    17, 32, 53, 78, 106, 134, 154, 192, 230, 271,
    321, 367, 425, 458, 520, 586, 644, 718, 792, 858,
    929, 1003, 1091, 1171, 1273
};

struct QrView {
    lv_obj_t obj;
    QRCode code;
    uint8_t* modules;
    lv_color_t dark;
    lv_color_t light;
};

static void qr_constructor(const lv_obj_class_t* object_class, lv_obj_t* object);
static void qr_destructor(const lv_obj_class_t* object_class, lv_obj_t* object);
static void qr_event(const lv_obj_class_t* object_class, lv_event_t* event);

static const lv_obj_class_t lvgl_qr_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = qr_constructor,
    .destructor_cb = qr_destructor,
    .event_cb = qr_event,
    .user_data = nullptr,
    .name = "qr",
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_FALSE,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(QrView),
    .theme_inheritable = LV_OBJ_CLASS_THEME_INHERITABLE_FALSE
};

static uint8_t* allocate_modules(uint8_t version) {
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    return static_cast<uint8_t*>(memory_alloc_with_policy(qrcode_getBufferSize(version), &policy));
}

static void qr_constructor(const lv_obj_class_t* object_class, lv_obj_t* object) {
    auto* view = reinterpret_cast<QrView*>(object);
    view->modules = nullptr;
    view->dark = lv_color_black();
    view->light = lv_color_white();
    // The view draws its own background and has nothing to scroll
    lv_obj_set_scrollable(object, false);
    lv_obj_set_style_pad_all(object, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(object, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(object, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_STATE_DEFAULT);
}

static void qr_destructor(const lv_obj_class_t* object_class, lv_obj_t* object) {
    auto* view = reinterpret_cast<QrView*>(object);
    if (view->modules != nullptr) {
        memory_free(view->modules);
        view->modules = nullptr;
    }
}

static void qr_draw(lv_obj_t* object, lv_layer_t* layer) {
    auto* view = reinterpret_cast<QrView*>(object);
    lv_area_t area;
    lv_obj_get_content_coords(object, &area);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.bg_color = view->light;
    lv_draw_rect(layer, &rect, &area);

    if (view->modules == nullptr) {
        return;
    }

    const int32_t modules_with_quiet_zone = view->code.size + 2 * QR_QUIET_ZONE_MODULES;
    const int32_t module_size = std::min(lv_area_get_width(&area), lv_area_get_height(&area)) / modules_with_quiet_zone;
    if (module_size < 1) {
        return;
    }
    const int32_t origin_x = area.x1 + (lv_area_get_width(&area) - modules_with_quiet_zone * module_size) / 2 + QR_QUIET_ZONE_MODULES * module_size;
    const int32_t origin_y = area.y1 + (lv_area_get_height(&area) - modules_with_quiet_zone * module_size) / 2 + QR_QUIET_ZONE_MODULES * module_size;

    rect.bg_color = view->dark;
    // Neighbouring dark modules in a row are drawn as one rectangle
    for (int32_t y = 0; y < view->code.size; y++) {
        int32_t x = 0;
        while (x < view->code.size) {
            if (!qrcode_getModule(&view->code, static_cast<uint8_t>(x), static_cast<uint8_t>(y))) {
                x++;
                continue;
            }
            int32_t end = x + 1;
            while (end < view->code.size && qrcode_getModule(&view->code, static_cast<uint8_t>(end), static_cast<uint8_t>(y))) {
                end++;
            }
            lv_area_t run = {
                .x1 = origin_x + x * module_size,
                .y1 = origin_y + y * module_size,
                .x2 = origin_x + end * module_size - 1,
                .y2 = origin_y + (y + 1) * module_size - 1,
            };
            lv_draw_rect(layer, &rect, &run);
            x = end;
        }
    }
}

static void qr_event(const lv_obj_class_t* object_class, lv_event_t* event) {
    if (lv_obj_event_base(&lvgl_qr_class, event) != LV_RESULT_OK) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DRAW_MAIN) {
        qr_draw(lv_event_get_current_target_obj(event), lv_event_get_layer(event));
    }
}

lv_obj_t* lvgl_qr_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lvgl_qr_class, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

error_t lvgl_qr_set_data(lv_obj_t* obj, const char* data) {
    auto* view = reinterpret_cast<QrView*>(obj);
    const size_t length = strlen(data);
    if (length > QR_VERSION_BYTE_CAPACITY[QR_MAX_VERSION - 1]) {
        return ERROR_OUT_OF_RANGE;
    }

    uint8_t version = 1;
    while (length > QR_VERSION_BYTE_CAPACITY[version - 1]) {
        version++;
    }
    uint8_t* modules = allocate_modules(version);
    if (modules == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    QRCode code;
    if (qrcode_initBytes(&code, modules, version, ECC_LOW, reinterpret_cast<uint8_t*>(const_cast<char*>(data)), static_cast<uint16_t>(length)) != 0) {
        memory_free(modules);
        return ERROR_OUT_OF_RANGE;
    }
    if (view->modules != nullptr) {
        memory_free(view->modules);
    }
    view->modules = modules;
    view->code = code;
    lv_obj_invalidate(obj);
    return ERROR_NONE;
}

int32_t lvgl_qr_get_min_size(lv_obj_t* obj) {
    auto* view = reinterpret_cast<QrView*>(obj);
    return view->modules == nullptr ? 0 : view->code.size + 2 * QR_QUIET_ZONE_MODULES;
}

void lvgl_qr_set_colors(lv_obj_t* obj, lv_color_t dark, lv_color_t light) {
    auto* view = reinterpret_cast<QrView*>(obj);
    view->dark = dark;
    view->light = light;
    lv_obj_invalidate(obj);
}
