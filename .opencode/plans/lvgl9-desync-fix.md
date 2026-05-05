# LVGL 9 Animation Desync Fix & Framerate Optimization

## Root Cause

The BSP demo uses `lvgl_port_add_disp_rgb()` which sets `disp_type = LVGL_PORT_DISP_TYPE_RGB`, causing `lv_disp_flush_ready()` to be called **synchronously** in the flush callback. This keeps LVGL's animation timer in lockstep with real time.

Tactility uses `lvgl_port_add_disp()` which sets `disp_type = LVGL_PORT_DISP_TYPE_OTHER`, skipping the synchronous `lv_disp_flush_ready()` and waiting for the **async** `on_color_trans_done` IO callback instead. This gap between frames causes animation desync.

---

## File 1: `Devices/waveshare-s3-touch-amoled-206/Source/devices/Display.cpp`

### Edit 1: Replace TE pin code with rounder callback (lines 19-29)
Remove the unused TE pin ISR/semaphore and add the rounder callback from the BSP:

```cpp
// REMOVE:
#define TE_GPIO GPIO_NUM_13
static SemaphoreHandle_t te_semaphore = NULL;
static void IRAM_ATTR te_isr_handler(void* arg) { ... }

// ADD:
static void rounder_event_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
```

### Edit 2: Remove `setup_te_pin()` declaration from class (line 65)
Remove `void setup_te_pin();` from the `Co5300Display` class definition.

### Edit 3: Replace `startLvgl()` body (lines 121-165)
- Remove `setup_te_pin();` call
- Change `lvgl_port_add_disp(&disp_cfg)` → `lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg)`
- Add rounder callback registration

```cpp
bool Co5300Display::startLvgl() {
    ESP_LOGI(TAG, "Registering display with LVGL port");
    if (panelHandle == nullptr || ioHandle == nullptr) {
        ESP_LOGE(TAG, "Display hardware not initialized");
        return false;
    }

    constexpr int buffer_rows = 52;
    constexpr int buffer_size = BSP_LCD_H_RES * buffer_rows;
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = ioHandle;
    disp_cfg.panel_handle = panelHandle;
    disp_cfg.buffer_size = (uint32_t)buffer_size;
    disp_cfg.double_buffer = false;
    disp_cfg.hres = BSP_LCD_H_RES;
    disp_cfg.vres = BSP_LCD_V_RES;
    disp_cfg.monochrome = false;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
#endif
    disp_cfg.flags.sw_rotate = true;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.buff_spiram = false;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.flags.swap_bytes = true;
#endif

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = 0,
            .avoid_tearing = 0,
        }
    };
    lvglDisplay = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvglDisplay == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return false;
    }

    lv_display_add_event_cb(lvglDisplay, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    ESP_LOGI(TAG, "Display registered with LVGL, lvglDisplay=%p", lvglDisplay);

    if (touch != nullptr && touch->supportsLvgl()) {
        touch->startLvgl(lvglDisplay);
    }
    return true;
}
```

### Edit 4: Remove TE cleanup from `stopLvgl()` (lines 170-174)
Remove the `if (te_semaphore != NULL) { ... }` block.

### Edit 5: Remove `setup_te_pin()` implementation (lines 95-119)
Delete the entire `Co5300Display::setup_te_pin()` method body.

---

## File 2: `sdkconfig` — LVGL Config Tuning

Three settings to change (via Kconfig menu or direct edit):

| Setting | Current | New | Rationale |
|---|---|---|---|
| `CONFIG_LV_DEF_REFR_PERIOD` | 33 | 15 | Match demo; 66.7 Hz target |
| `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT` | 1 | 2 | Parallel rendering for throughput |
| `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` | not set | y | Draw functions in IRAM for speed |

To apply via sdkconfig editor:
```bash
idf.py menuconfig
# Navigate to: Component config → LVGL → Rendering
```

Or direct sdkconfig edits:
```ini
# Change line ~2670
CONFIG_LV_DEF_REFR_PERIOD=15

# Change line ~2680
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2

# Add near LV_ATTRIBUTE_FAST_MEM section
CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y
```

Also recommend enabling for profiling:
```ini
CONFIG_LV_USE_SYSMON=y
CONFIG_LV_USE_PERF_MONITOR=y
```

---

## Verification

After changes:
1. Build: `idf.py build`
2. Flash and verify animations are smooth (no desync)
3. Check FPS counter (if Sysmon enabled) — should show steady ~60-67 fps
