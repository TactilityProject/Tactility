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
 * A dedicated (not the waiting task's shared default FreeRTOS notification, which other
 * subsystems - e.g. app_event.cpp's AppEventSubscription - also use; an unrelated notification
 * delivered to the same task could otherwise unblock a wait early) completion signal for one
 * window_manager_await_state_change() call. Heap-allocated with its own refcount (protected by
 * WindowManagerState::mutex, not atomic) rather than owned solely by the WindowRecord, since
 * window_manager_create()/remove() claim (read + clear) a window's signal under the lock but
 * give it after releasing that lock - refcounting lets whichever side (the waiting task waking
 * up, or the claimer after it gives) finishes last safely delete it.
 */
struct WindowWaitSignal {
    SemaphoreHandle_t semaphore;
    /** Starts at 1, owned by window_manager_await_state_change() until it's done waiting.
     * Whoever claims this signal from a WindowRecord (see claim_waiter_locked()) takes an
     * additional reference for as long as it takes to give the semaphore. Reaching 0 means
     * deletion. */
    int refcount = 1;
};

struct WindowRecord {
    WindowId id;
    uint32_t app_instance_id;
    WindowCreateWidgetsFn create_widgets;
    void* user_data;

    /** Set by window_manager_await_state_change() for this specific window, if a task is
     * currently blocked there - see that function's @warning on at most one concurrent awaiter
     * per window. Per-window rather than a single manager-wide slot, since a stacked window
     * manager serving several app tasks can have more than one window (though only ever one of
     * them topmost/GRANTED at a time) with a live await() call outstanding. */
    WindowWaitSignal* waiting_signal = nullptr;
};

struct WindowManagerState {
    /** Mutex for read/write operations. Shortly held. */
    Mutex mutex {};

    /** Serializes the full start()/stop()/create()/remove() transitions against each other,
     * including the LVGL work done with `mutex` released (and any create_widgets()/
     * screen_init() callback invoked as part of that work). Without this, e.g.
     * window_manager_stop() could delete real_root_widget/content_root_widget/top_widget
     * between a concurrent create()/remove() capturing one of those pointers under `mutex` and
     * actually using it via build_window_widget()/delete_widget() after releasing `mutex` -
     * touching an LVGL object it no longer holds a valid reference to. */
    Mutex lifecycle_mutex {};

    bool started = false;
    WindowManagerScreenInitFn screen_init = nullptr;

    /** The raw, full-size container window_manager_start() creates; owns (and deletion
     * cascades to) whatever the screen-init callback added under it. */
    lv_obj_t* real_root_widget = nullptr;
    /** The stable parent each window's own widget is created under - real_root_widget itself,
     * unless the screen-init callback returned a nested content widget instead. */
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
    if (create_widgets != nullptr) {
        create_widgets(widget, user_data);
    }
    lvgl_unlock();
    return widget;
}

void delete_widget(lv_obj_t* widget) {
    if (widget == nullptr) {
        return;
    }
    lvgl_lock();
    lv_obj_delete(widget);
    lvgl_unlock();
}

// Call while holding WindowManagerState::mutex. Transfers ownership of `window`'s waiting
// signal (if any) to the caller, taking an additional reference on the caller's behalf - the
// caller must eventually pass the result to give_and_release() exactly once, outside the lock.
WindowWaitSignal* claim_waiter_locked(WindowRecord& window) {
    WindowWaitSignal* signal = window.waiting_signal;
    window.waiting_signal = nullptr;
    if (signal != nullptr) {
        signal->refcount++;
    }
    return signal;
}

// Gives `signal`'s semaphore (waking window_manager_await_state_change() if it's still
// waiting) and releases the caller's reference (see claim_waiter_locked()), deleting the
// signal if that was the last one. No-op if `signal` is NULL.
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

    // Held for the whole transition (including the LVGL work below, done with `mutex`
    // released) - blocks a concurrent start() from also passing the `started` check and
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

    mutex_lock(&s.mutex);
    s.real_root_widget = real_widget;
    s.content_root_widget = content_widget;
    s.started = true;
    mutex_unlock(&s.mutex);

    mutex_unlock(&s.lifecycle_mutex);
    return ERROR_NONE;
}

error_t window_manager_stop(void) {
    auto& s = state();

    // See window_manager_start() - blocks until any in-flight start() has fully completed (or
    // failed) before this stop can observe/tear down state.
    mutex_lock(&s.lifecycle_mutex);

    mutex_lock(&s.mutex);
    if (!s.started) {
        mutex_unlock(&s.mutex);
        mutex_unlock(&s.lifecycle_mutex);
        return ERROR_NONE;
    }
    lv_obj_t* widget = s.real_root_widget;
    // Claim every window's waiter before clearing - normally at most the topmost window's is
    // ever set, but every window is being torn down here, so every one is checked.
    std::vector<WindowWaitSignal*> waiters;
    for (auto& window : s.windows) {
        if (auto* signal = claim_waiter_locked(window); signal != nullptr) {
            waiters.push_back(signal);
        }
    }
    s.real_root_widget = nullptr;
    s.content_root_widget = nullptr;
    s.top_widget = nullptr;
    s.windows.clear();
    s.started = false;
    mutex_unlock(&s.mutex);

    for (WindowWaitSignal* waiter : waiters) {
        give_and_release(waiter);
    }

    // Deleting the real widget cascades to everything under it - chrome and top_widget alike.
    delete_widget(widget);

    mutex_unlock(&s.lifecycle_mutex);
    return ERROR_NONE;
}

WindowId window_manager_create(AppInstanceId app_instance_id, WindowCreateWidgetsFn create_widgets, void* user_data) {
    if (app_instance_id == 0) {
        return 0;
    }

    auto& s = state();

    // See lifecycle_mutex's comment - blocks a concurrent window_manager_stop() (or another
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
    // The current topmost window (if any) is about to be superseded - claim its waiter (if
    // any) here so it gets notified below, since it's no longer topmost after this.
    WindowWaitSignal* waiter = !s.windows.empty() ? claim_waiter_locked(s.windows.back()) : nullptr;
    s.top_widget = nullptr;
    WindowId new_id = s.next_id++;
    s.windows.push_back(WindowRecord { new_id, app_instance_id, create_widgets, user_data });
    mutex_unlock(&s.mutex);

    give_and_release(waiter);

    delete_widget(old_top_widget);
    lv_obj_t* new_widget = build_window_widget(content, create_widgets, user_data);

    mutex_lock(&s.mutex);
    bool still_topmost = !s.windows.empty() && s.windows.back().id == new_id;
    if (still_topmost) {
        s.top_widget = new_widget;
        new_widget = nullptr; // consumed
    }
    mutex_unlock(&s.mutex);

    // Something else became topmost while we were building (e.g. a concurrent create() from
    // another app thread) - discard what we just made.
    delete_widget(new_widget);

    mutex_unlock(&s.lifecycle_mutex);
    return new_id;
}

void window_manager_remove(WindowId id) {
    auto& s = state();

    // See lifecycle_mutex's comment - blocks a concurrent window_manager_stop() (or another
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
    // The window being removed owns its own waiter (if any) - a waiter is only ever registered
    // while its window is topmost (see window_manager_await_state_change()), and if this window
    // later stopped being topmost without being removed, window_manager_create() would already
    // have claimed/cleared it - so a buried window's waiting_signal is always already null.
    WindowWaitSignal* waiter = claim_waiter_locked(*iterator);
    s.windows.erase(iterator);

    lv_obj_t* content = s.content_root_widget;
    lv_obj_t* old_widget = nullptr;
    WindowCreateWidgetsFn next_create_widgets = nullptr;
    void* next_user_data = nullptr;
    WindowId next_id = 0;
    bool has_next = false;

    if (was_topmost) {
        old_widget = s.top_widget;
        s.top_widget = nullptr;
        if (!s.windows.empty()) {
            next_create_widgets = s.windows.back().create_widgets;
            next_user_data = s.windows.back().user_data;
            next_id = s.windows.back().id;
            has_next = true;
        }
    }
    mutex_unlock(&s.mutex);

    give_and_release(waiter);

    if (!was_topmost) {
        // A buried window was removed - the topmost window's widgets are unaffected.
        mutex_unlock(&s.lifecycle_mutex);
        return;
    }

    delete_widget(old_widget);
    lv_obj_t* new_widget = has_next ? build_window_widget(content, next_create_widgets, next_user_data) : nullptr;

    mutex_lock(&s.mutex);
    bool still_topmost = has_next && !s.windows.empty() && s.windows.back().id == next_id;
    if (still_topmost) {
        s.top_widget = new_widget;
        new_widget = nullptr; // consumed
    }
    mutex_unlock(&s.mutex);

    delete_widget(new_widget);

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

    // Dedicated semaphore rather than this task's default FreeRTOS notification - other
    // subsystems (e.g. app_event.cpp's AppEventSubscription) use that same shared slot, so an
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
    // At most one concurrent awaiter per window - see the @warning on this function.
    check(s.windows.back().waiting_signal == nullptr);
    s.windows.back().waiting_signal = signal;
    mutex_unlock(&s.mutex);

    xSemaphoreTake(signal->semaphore, timeout);

    // Deregister ourselves if a create()/remove() hasn't already claimed us (the ordinary,
    // intended wakeup) - otherwise a later create()/remove() could read a signal that's already
    // been given away here. Re-locate the record by id - it may have been erased
    // (window_manager_remove()) while we waited. Either way, release our own reference:
    // whichever side (us or a claimer) does this last is the one that actually deletes it.
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
