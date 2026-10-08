// SPDX-License-Identifier: Apache-2.0
#include <lvgl_window_manager/window_manager.h>

#include <app/instance.h>

#include <lvgl/lvgl.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/semphr.h>

#include <algorithm>
#include <new>
#include <vector>

constexpr auto* TAG = "window_manager";

namespace {

/**
 * Completion signal for a single window_manager_await_state_change() call.
 *
 * Heap-allocated with its own refcount, protected by WindowManagerState::mutex (not atomic).
 * It can't be owned solely by the WindowRecord: window_manager_create()/remove() claim
 * (read + clear) a window's signal under the lock, then give it after releasing that lock.
 * The refcount lets whichever side finishes last - the waiting task waking up, or the
 * claimer after giving the semaphore - safely delete it.
 */
struct WindowWaitSignal {
    SemaphoreHandle_t semaphore;
    /** Starts at 1, owned by window_manager_await_state_change() until it's done waiting.
     * Whoever claims this signal from a WindowRecord (see claim_waiter_locked()) takes an
     * extra reference for as long as it takes to give the semaphore. Reaching 0 deletes it. */
    int refcount = 1;
};

struct WindowRecord {
    WindowId id;
    uint32_t app_instance_id;
    WindowCreateWidgetsFn create_widgets;
    WindowDestroyWidgetsFn destroy_widgets;
    void* user_data;

    /** Set by window_manager_await_state_change() when a task is blocked waiting on this
     * window (see that function's @warning: at most one concurrent awaiter per window).
     * Per-window rather than a single manager-wide slot, because a stacked window manager
     * serving several app tasks can have more than one window with a live await() call
     * outstanding, even though only one is ever topmost/GRANTED at a time. */
    WindowWaitSignal* waiting_signal = nullptr;
};

struct WindowManagerState {
    /** Mutex for read/write operations. Shortly held. */
    Mutex mutex {};

    /** Serializes the full start()/stop()/create()/remove() transitions against each other,
     * including LVGL work done after `mutex` is released, such as a create_widgets() or
     * screen_init() callback. Without it, window_manager_stop() could free
     * real_root_widget/content_root_widget/top_widget out from under a concurrent create() or
     * remove() that captured one of those pointers under `mutex` but only uses it afterward,
     * via build_window_widget()/delete_widget(). */
    Mutex lifecycle_mutex {};

    bool started = false;
    WindowManagerScreenInitFn screen_init = nullptr;

    /** The raw, full-size container window_manager_start() creates; owns (and deletion
     * cascades to) whatever the screen-init callback added under it. */
    lv_obj_t* real_root_widget = nullptr;
    /** The stable parent each window's own widget is created under. Normally
     * real_root_widget itself, but the screen-init callback may return a nested content
     * widget to use instead. */
    lv_obj_t* content_root_widget = nullptr;

    WindowId next_id = 1;
    /** windows.back() is topmost; only it ever has a live widget (top_widget). */
    std::vector<WindowRecord> windows;
    lv_obj_t* top_widget = nullptr;

    WindowManagerState() {
        mutex_construct(&mutex);
        mutex_construct(&lifecycle_mutex);
    }
};

WindowManagerState& state() {
    static WindowManagerState instance;
    return instance;
}

lv_obj_t* build_window_widget(lv_obj_t* content, WindowCreateWidgetsFn create_widgets, void* user_data) {
    if (content == nullptr) {
        return nullptr;
    }
    lvgl_lock();
    lv_obj_t* widget = lv_obj_create(content);
    lv_obj_set_size(widget, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(widget, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(widget, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(widget, 0, LV_STATE_DEFAULT);
    // Plain layout container, not meant to scroll on its own - every app already does this
    // for its own root object. Without it, a sub-pixel flex-layout overflow here can show the
    // theme's scrollbar styling as a thin line hugging this widget's edges.
    lv_obj_set_scrollable(widget, false);
    if (create_widgets != nullptr) {
        create_widgets(widget, user_data);
    }
    lvgl_unlock();
    return widget;
}

// destroy_widgets, if set, is called inside the same LVGL-locked section as the deletion - see
// WindowDestroyWidgetsFn's warnings about what it may safely do from in here.
void delete_widget(lv_obj_t* widget, WindowDestroyWidgetsFn destroy_widgets = nullptr, void* user_data = nullptr) {
    if (widget == nullptr) {
        return;
    }
    lvgl_lock();
    if (destroy_widgets != nullptr) {
        destroy_widgets(user_data);
    }
    lv_obj_delete(widget);
    lvgl_unlock();
}

// Call while holding WindowManagerState::mutex. Transfers ownership of `window`'s waiting
// signal, if any, to the caller, taking an extra reference on the caller's behalf. The
// caller must pass the result to give_and_release() exactly once, outside the lock.
WindowWaitSignal* claim_waiter_locked(WindowRecord& window) {
    WindowWaitSignal* signal = window.waiting_signal;
    window.waiting_signal = nullptr;
    if (signal != nullptr) {
        signal->refcount++;
    }
    return signal;
}

// Gives `signal`'s semaphore, waking window_manager_await_state_change() if it's still
// waiting, then releases the caller's reference from claim_waiter_locked(). Deletes the
// signal if that was the last reference. No-op if `signal` is NULL.
void give_and_release(WindowWaitSignal* signal) {
    if (signal == nullptr) {
        return;
    }

    xSemaphoreGive(signal->semaphore);

    auto& s = state();
    mutex_lock(&s.mutex);
    bool should_delete = (--signal->refcount == 0);
    mutex_unlock(&s.mutex);
    if (should_delete) {
        vSemaphoreDelete(signal->semaphore);
        delete signal;
    }
}

} // namespace

extern "C" {

void window_manager_configure(WindowManagerScreenInitFn screen_init) {
    auto& s = state();

    // Serializes against window_manager_start()/stop()
    mutex_lock(&s.lifecycle_mutex);

    mutex_lock(&s.mutex);
    if (!s.started) {
        s.screen_init = screen_init;
    } else {
        LOG_W(TAG, "Ignoring window_manager_configure: module is already started");
    }
    mutex_unlock(&s.mutex);

    mutex_unlock(&s.lifecycle_mutex);
}

error_t window_manager_start(void) {
    auto& s = state();

    // Held for the whole transition, including the LVGL work below done with `mutex`
    // released. Blocks a concurrent start() from also passing the `started` check and
    // building its own root widget, and blocks a concurrent stop() from running while this
    // start is still mid-flight.
    mutex_lock(&s.lifecycle_mutex);

    mutex_lock(&s.mutex);
    if (s.started) {
        mutex_unlock(&s.mutex);
        mutex_unlock(&s.lifecycle_mutex);
        return ERROR_NONE;
    }
    WindowManagerScreenInitFn screen_init = s.screen_init;
    mutex_unlock(&s.mutex);

    lv_obj_t* real_widget = nullptr;
    lv_obj_t* content_widget = nullptr;

    lvgl_lock();
    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr) {
        real_widget = lv_obj_create(screen);
        lv_obj_set_size(real_widget, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(real_widget, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(real_widget, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(real_widget, 0, LV_STATE_DEFAULT);
        // See build_window_widget()'s identical flag removal for why.
        lv_obj_set_scrollable(real_widget, false);

        content_widget = (screen_init != nullptr) ? screen_init(real_widget) : nullptr;
        if (content_widget == nullptr) {
            content_widget = real_widget;
        }
    }
    lvgl_unlock();

    if (real_widget == nullptr) {
        mutex_unlock(&s.lifecycle_mutex);
        return ERROR_RESOURCE;
    }

    // A previous stop() may have left window records behind for an app that's still running
    // (see window_manager_stop()'s comment). Rebuild the topmost one now, the same way
    // window_manager_remove() rebuilds when a buried window resurfaces. Otherwise that app's
    // task stays blocked in its own event loop forever, with no window and no signal telling
    // it to rebuild one.
    WindowCreateWidgetsFn top_create_widgets = nullptr;
    WindowDestroyWidgetsFn top_destroy_widgets = nullptr;
    void* top_user_data = nullptr;
    WindowId top_id = 0;
    bool has_top = false;

    mutex_lock(&s.mutex);
    s.real_root_widget = real_widget;
    s.content_root_widget = content_widget;
    s.started = true;
    if (!s.windows.empty()) {
        top_create_widgets = s.windows.back().create_widgets;
        top_destroy_widgets = s.windows.back().destroy_widgets;
        top_user_data = s.windows.back().user_data;
        top_id = s.windows.back().id;
        has_top = true;
    }
    mutex_unlock(&s.mutex);

    if (has_top) {
        lv_obj_t* new_widget = build_window_widget(content_widget, top_create_widgets, top_user_data);

        mutex_lock(&s.mutex);
        bool still_topmost = !s.windows.empty() && s.windows.back().id == top_id;
        if (still_topmost) {
            s.top_widget = new_widget;
            new_widget = nullptr; // consumed
        }
        mutex_unlock(&s.mutex);

        // The window stack changed while we were building, e.g. a concurrent remove() -
        // discard what we just made.
        delete_widget(new_widget, top_destroy_widgets, top_user_data);
    }

    mutex_unlock(&s.lifecycle_mutex);
    return ERROR_NONE;
}

error_t window_manager_stop(void) {
    auto& s = state();

    // See window_manager_start(): blocks until any in-flight start() has finished, or failed,
    // before this stop observes or tears down state.
    mutex_lock(&s.lifecycle_mutex);

    mutex_lock(&s.mutex);
    if (!s.started) {
        mutex_unlock(&s.mutex);
        mutex_unlock(&s.lifecycle_mutex);
        return ERROR_NONE;
    }
    lv_obj_t* widget = s.real_root_widget;
    // Claim every window's waiter before tearing down. Normally only the topmost window has
    // one set, but every window's widget is torn down here, so every one is checked.
    std::vector<WindowWaitSignal*> waiters;
    for (auto& window : s.windows) {
        if (auto* signal = claim_waiter_locked(window); signal != nullptr) {
            waiters.push_back(signal);
        }
    }
    // Only the topmost window has a live widget - it's the only one whose destroy_widgets needs
    // to fire.
    WindowDestroyWidgetsFn top_destroy_widgets = !s.windows.empty() ? s.windows.back().destroy_widgets : nullptr;
    void* top_user_data = !s.windows.empty() ? s.windows.back().user_data : nullptr;
    s.real_root_widget = nullptr;
    s.content_root_widget = nullptr;
    s.top_widget = nullptr;
    // Deliberately not s.windows.clear(): this tears down only the LVGL widget tree, not the
    // window records. On a real full shutdown every app has already removed its own window via
    // window_manager_remove(), so the list is empty anyway and this is a no-op. But a caller can
    // also stop()/start() this module on its own, temporarily, while apps keep running
    // underneath - for example one borrowing the display/touch hardware directly. Those apps'
    // tasks stay alive, blocked in their own event loops, with no way to know they need to call
    // window_manager_create() again. Keeping the records lets window_manager_start() rebuild the
    // topmost one automatically instead of leaving that app stuck with no window forever.
    s.started = false;
    mutex_unlock(&s.mutex);

    for (WindowWaitSignal* waiter : waiters) {
        give_and_release(waiter);
    }

    // Deleting the real widget cascades to everything under it - chrome and top_widget alike.
    delete_widget(widget, top_destroy_widgets, top_user_data);

    mutex_unlock(&s.lifecycle_mutex);
    return ERROR_NONE;
}

WindowId window_manager_create(AppInstanceId app_instance_id, WindowCreateWidgetsFn create_widgets, void* user_data) {
    return window_manager_create_ext(app_instance_id, create_widgets, nullptr, user_data);
}

WindowId window_manager_create_ext(AppInstanceId app_instance_id, WindowCreateWidgetsFn create_widgets, WindowDestroyWidgetsFn destroy_widgets, void* user_data) {
    if (app_instance_id == 0) {
        return 0;
    }

    if (destroy_widgets != nullptr) {
        check(create_widgets != nullptr);
    }

    auto& s = state();

    // See lifecycle_mutex's comment: blocks a concurrent window_manager_stop() (or another
    // create()/remove()) from touching real_root_widget/content_root_widget/top_widget while
    // this call still holds pointers to them.
    mutex_lock(&s.lifecycle_mutex);

    mutex_lock(&s.mutex);
    if (!s.started) {
        mutex_unlock(&s.mutex);
        mutex_unlock(&s.lifecycle_mutex);
        return 0;
    }
    lv_obj_t* content = s.content_root_widget;
    lv_obj_t* old_top_widget = s.top_widget;
    // The current topmost window, if any, is about to be superseded - claim its waiter here
    // so it gets notified below, and grab its destroy_widgets so it can be told its widget is
    // about to go away.
    WindowWaitSignal* waiter = nullptr;
    WindowDestroyWidgetsFn old_destroy_widgets = nullptr;
    void* old_user_data = nullptr;
    if (!s.windows.empty()) {
        waiter = claim_waiter_locked(s.windows.back());
        old_destroy_widgets = s.windows.back().destroy_widgets;
        old_user_data = s.windows.back().user_data;
    }
    s.top_widget = nullptr;
    WindowId new_id = s.next_id++;
    s.windows.push_back(WindowRecord { new_id, app_instance_id, create_widgets, destroy_widgets, user_data });
    mutex_unlock(&s.mutex);

    give_and_release(waiter);

    delete_widget(old_top_widget, old_destroy_widgets, old_user_data);
    lv_obj_t* new_widget = build_window_widget(content, create_widgets, user_data);

    mutex_lock(&s.mutex);
    bool still_topmost = !s.windows.empty() && s.windows.back().id == new_id;
    if (still_topmost) {
        s.top_widget = new_widget;
        new_widget = nullptr; // consumed
    }
    mutex_unlock(&s.mutex);

    // Another window became topmost while we were building, e.g. a concurrent create() from
    // another app thread - discard what we just made.
    delete_widget(new_widget, destroy_widgets, user_data);

    mutex_unlock(&s.lifecycle_mutex);
    return new_id;
}

void window_manager_remove(WindowId id) {
    auto& s = state();

    // See lifecycle_mutex's comment: blocks a concurrent window_manager_stop() (or another
    // create()/remove()) from touching real_root_widget/content_root_widget/top_widget while
    // this call still holds pointers to them.
    mutex_lock(&s.lifecycle_mutex);

    mutex_lock(&s.mutex);
    auto iterator = std::find_if(s.windows.begin(), s.windows.end(),
        [id](const WindowRecord& window) { return window.id == id; });
    if (iterator == s.windows.end()) {
        mutex_unlock(&s.mutex);
        mutex_unlock(&s.lifecycle_mutex);
        return;
    }
    bool was_topmost = (iterator + 1 == s.windows.end());
    // The window being removed owns its own waiter, if any. A waiter is only ever registered
    // while its window is topmost (see window_manager_await_state_change()); if this window had
    // since stopped being topmost without being removed, window_manager_create() would already
    // have claimed and cleared it. So a buried window's waiting_signal is always already null.
    WindowWaitSignal* waiter = claim_waiter_locked(*iterator);
    WindowDestroyWidgetsFn removed_destroy_widgets = iterator->destroy_widgets;
    void* removed_user_data = iterator->user_data;
    s.windows.erase(iterator);

    lv_obj_t* content = s.content_root_widget;
    lv_obj_t* old_widget = nullptr;
    WindowCreateWidgetsFn next_create_widgets = nullptr;
    WindowDestroyWidgetsFn next_destroy_widgets = nullptr;
    void* next_user_data = nullptr;
    WindowId next_id = 0;
    bool has_next = false;

    if (was_topmost) {
        old_widget = s.top_widget;
        s.top_widget = nullptr;
        if (!s.windows.empty()) {
            next_create_widgets = s.windows.back().create_widgets;
            next_destroy_widgets = s.windows.back().destroy_widgets;
            next_user_data = s.windows.back().user_data;
            next_id = s.windows.back().id;
            has_next = true;
        }
    }
    mutex_unlock(&s.mutex);

    give_and_release(waiter);

    if (!was_topmost) {
        // A buried window was removed; the topmost window's widgets are unaffected.
        mutex_unlock(&s.lifecycle_mutex);
        return;
    }

    delete_widget(old_widget, removed_destroy_widgets, removed_user_data);
    lv_obj_t* new_widget = has_next ? build_window_widget(content, next_create_widgets, next_user_data) : nullptr;

    mutex_lock(&s.mutex);
    bool still_topmost = has_next && !s.windows.empty() && s.windows.back().id == next_id;
    if (still_topmost) {
        s.top_widget = new_widget;
        new_widget = nullptr; // consumed
    }
    mutex_unlock(&s.mutex);

    delete_widget(new_widget, next_destroy_widgets, next_user_data);

    mutex_unlock(&s.lifecycle_mutex);
}

WindowState window_manager_get_state(WindowId id) {
    auto& s = state();
    mutex_lock(&s.mutex);
    bool is_top = !s.windows.empty() && s.windows.back().id == id;
    mutex_unlock(&s.mutex);
    return is_top ? WINDOW_STATE_GRANTED : WINDOW_STATE_REVOKED;
}

WindowState window_manager_await_state_change(WindowId id, TickType_t timeout) {
    auto& s = state();

    // Uses a dedicated semaphore rather than this task's default FreeRTOS notification.
    // Other subsystems, e.g. app_event.cpp's AppEventSubscription, share that same slot - an
    // unrelated notification delivered to this task could otherwise wake this wait early.
    auto* signal = new (std::nothrow) WindowWaitSignal();
    if (signal == nullptr) {
        return window_manager_get_state(id);
    }
    signal->semaphore = xSemaphoreCreateBinary();
    if (signal->semaphore == nullptr) {
        delete signal;
        return window_manager_get_state(id);
    }

    mutex_lock(&s.mutex);
    bool is_top = !s.windows.empty() && s.windows.back().id == id;
    if (!is_top) {
        mutex_unlock(&s.mutex);
        vSemaphoreDelete(signal->semaphore);
        delete signal;
        return WINDOW_STATE_REVOKED;
    }
    // At most one concurrent awaiter per window; see this function's @warning.
    check(s.windows.back().waiting_signal == nullptr);
    s.windows.back().waiting_signal = signal;
    mutex_unlock(&s.mutex);

    xSemaphoreTake(signal->semaphore, timeout);

    // Deregister ourselves if a create()/remove() hasn't already claimed us. This is the
    // ordinary, intended wakeup path; without it, a later create()/remove() could read a
    // signal that's already been given away here. Re-locate the record by id, since it may
    // have been erased by window_manager_remove() while we waited. Either way, release our
    // own reference - whichever side finishes last, us or a claimer, is the one that deletes
    // it.
    mutex_lock(&s.mutex);
    auto iterator = std::find_if(s.windows.begin(), s.windows.end(),
        [id](const WindowRecord& window) { return window.id == id; });
    if (iterator != s.windows.end() && iterator->waiting_signal == signal) {
        iterator->waiting_signal = nullptr;
    }
    bool should_delete = (--signal->refcount == 0);
    mutex_unlock(&s.mutex);
    if (should_delete) {
        vSemaphoreDelete(signal->semaphore);
        delete signal;
    }

    return window_manager_get_state(id);
}

} // extern "C"
