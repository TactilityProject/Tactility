# Architecture: LVGL

User interfaces should scale well for everything between very large (e.g. 1280x720) and small (e.g. 135x240) displays. Vertical and horizontal layouts are supported.

Two kernel modules cover LVGL:

- **`lvgl-module`** (`Modules/lvgl-module/`, `<lvgl/*.h>`) owns LVGL's lifecycle: init/deinit, the LVGL task loop, and `lvgl_lock()`/`lvgl_try_lock()`/`lvgl_unlock()` mutex-based locking that any task must hold before touching LVGL objects. It bridges Tactility's device model to LVGL indevs (`lvgl/devices/*.h`: `display`, `pointer`, `keyboard`, `trackball`), and provides shared fonts (`lvgl/fonts.h`: Montserrat text sizes, Material Symbols icon sets for statusbar/launcher/shared use) and a few shared widgets (`lvgl/widgets/*.h`: `toolbar`, `spinner`, `sliderbox`).
- **`lvgl-window-manager-module`** (`Modules/lvgl-window-manager-module/`, `<lvgl_window_manager/*.h>`) manages a single stacked window per app instance on top of `lvgl-module`. `window_manager_start()`/`window_manager_stop()` create/tear down the root widget (plus optional chrome from a configured `WindowManagerScreenInitFn`); `window_manager_create()`/`window_manager_remove()` push/pop an app's window and (re)populate it via a `WindowCreateWidgetsFn`. Only the topmost window ever has live widgets - burying and resurfacing a window deletes and rebuilds its widget tree rather than hiding/showing it. That rebuild-on-remove path can run `create_widgets` on a *different* app's thread (whichever app's `window_manager_remove()` call caused this window to resurface), so `create_widgets` must only rebuild already-committed state, never decide what happens next - state transitions belong in the app's own `main()` event loop, driven by real `APP_EVENT_RESULT`s.

## Theming (LSS)

The themes in `Modules/lvgl-module/source/themes/lv_theme_material*.c/.h` are generated: never edit them directly. They're compiled from the LVGL Style Sheets in `Modules/lvgl-module/themes/*.lss` by the LSS tool, a git submodule at `Modules/lvgl-module/lss/` (see its `README.md` for the syntax).

- Styles are defined once and attached to widget types with `apply(<widget>) { <style>: <state/part>; }` rules. Custom widget classes of `lvgl-module` (e.g. `lvgl_card_class`) are declared with `@widget <name>: <class>;`.
- Change `material.lss` and `material_mono.lss` together, then regenerate (requires `pip install lark`):

```bash
cd Modules/lvgl-module
python lss/compile.py themes/material.lss --output source/themes
python lss/compile.py themes/material_mono.lss --output source/themes
```
