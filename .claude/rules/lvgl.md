# Architecture: LVGL

User interfaces should scale well for everything between very large (e.g. 1280x720) and small (e.g. 135x240) displays. Vertical and horizontal layouts are supported.
Margins and padding are set in density-independent pixels with `LV_DPX()`, not in raw pixels.

Two kernel modules cover LVGL:

- **`lvgl-module`** (`Modules/lvgl-module/`, `<lvgl/*.h>`) owns LVGL's lifecycle: init/deinit, the LVGL task loop, and `lvgl_lock()`/`lvgl_try_lock()`/`lvgl_unlock()` mutex-based locking that any task must hold before touching LVGL objects. It bridges Tactility's device model to LVGL indevs (`lvgl/devices/*.h`: `display`, `pointer`, `keyboard`, `trackball`), and provides shared fonts (`lvgl/fonts.h`: Montserrat text sizes, Material Symbols icon sets for statusbar/launcher/shared use) and a few shared widgets (`lvgl/widgets/*.h`: `toolbar`, `spinner`, `sliderbox`).
- **`lvgl-window-manager-module`** (`Modules/lvgl-window-manager-module/`, `<lvgl_window_manager/*.h>`) manages a single stacked window per app instance on top of `lvgl-module`. It also owns the root layout (statusbar, app container and page bar) with a public API in `<lvgl_window_manager/system_bars.h>`: `system_bars_configure()` takes the callbacks that Tactility provides (layout resolver, system menu, extra screen widgets), apps use `system_bars_set_main_action()` for one main action icon (hidden until set) and `system_bars_set_page_button_callbacks()` and `system_bars_set_page_buttons_visible()` for the previous and next page buttons. The window manager resets all of them when the window's widgets are deleted. `window_manager_start()`/`window_manager_stop()` create/tear down the root widget (plus optional chrome from a configured `WindowManagerScreenInitFn`); `window_manager_create()`/`window_manager_remove()` push/pop an app's window and (re)populate it via a `WindowCreateWidgetsFn`. Only the topmost window ever has live widgets - burying and resurfacing a window deletes and rebuilds its widget tree rather than hiding/showing it. That rebuild-on-remove path can run `create_widgets` on a *different* app's thread (whichever app's `window_manager_remove()` call caused this window to resurface), so `create_widgets` must only rebuild already-committed state, never decide what happens next - state transitions belong in the app's own `main()` event loop, driven by real `APP_EVENT_RESULT`s.
  Widget pointers an app keeps become dangling once its window is buried (e.g. by a dialog it started). Use `window_manager_create_ext()` with a `destroy_widgets` callback that clears them, and skip widget updates from the app's own thread while they're cleared.

## Lists in cards

Every list item has a divider below it, so the last one lies on the border of the card that holds the list.
Call `lvgl_list_hide_last_divider()` (`lvgl/widgets/list.h`) on the list, or on the card when the items are its direct children, after the last item was added.

## No file operations on the LVGL thread

Code that runs on the LVGL thread must not do file operations (including reading or writing settings): `create_widgets`, LVGL event callbacks, timers, and anything under the LVGL lock.
A file read can block the whole UI, and `create_widgets` can run on another app's thread with a small stack, or a stack in external RAM that can't access flash.

- Do the file work in the app's own task and keep the results in the app's context. `create_widgets` and update functions only show what's cached.
- Callbacks signal the app's task, e.g. with an event group bit.
- The same applies to calls that block on a service, e.g. `mesh_*` calls that wait for the service thread.

## Keyboard navigation

- Put groups of related focusable widgets (pills/chips, rows of buttons, toggle tiles) in a grid navigation container with `lvgl_grid_navigation_add()` (`lvgl/grid_navigation.h`). Only the container joins the input group: left/right moves within the group and up/down moves between groups.
- Never make checkable widgets (switches, checkable buttons) direct input group members: LVGL toggles them on arrow keys. Grid navigation forwards only Enter to them.

## Headers with a close button

Apps that replace the toolbar with a custom header (close button, tabs, actions) must make every group of the header reachable with the arrow keys:

- A plain button ignores left and right. Put the close button in its own container with `lvgl_grid_navigation_add()`, next to the tabs and the actions, which are grid navigation groups too. Moving past the edge of a group moves the focus to the previous or next group.
- Call `lvgl_grid_navigation_focus_without_touch(container, closeButton)` after that. Without a touchscreen the close button is selected when the app starts, as `lvgl_toolbar_create()` does, so the user doesn't have to move the focus there first.
- Cards and other content that don't take input but are taller than the display (e.g. the usage cards of System Info) must be focusable (clickable and click-focusable, in a grid navigation container), so the keys can scroll to them on small displays.
- The focus group only holds the content of the shown tab: remove the containers of hidden tabs from the group.

## Theming (LSS)

The themes in `Modules/lvgl-module/source/themes/lv_theme_material*.c/.h` are generated: never edit them directly. They're compiled from the LVGL Style Sheets in `Modules/lvgl-module/themes/*.lss` by the LSS tool, a git submodule at `Modules/lvgl-module/lss/` (see its `README.md` for the syntax).

- Styles are defined once and attached to widget types with `apply(<widget>) { <style>: <state/part>; }` rules. Custom widget classes of `lvgl-module` (e.g. `lvgl_card_class`) are declared with `@widget <name>: <class>;`.
- Change `material.lss` and `material_mono.lss` together, then regenerate (requires `pip install lark`):

```bash
cd Modules/lvgl-module
python lss/compile.py themes/material.lss --output source/themes
python lss/compile.py themes/material_mono.lss --output source/themes
```
