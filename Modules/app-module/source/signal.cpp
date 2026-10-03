// SPDX-License-Identifier: Apache-2.0
#include <app/signal.h>

#include <app/libc.h>
#include <app/manager.h>
#include <app/private/event.h>
#include <app/private/ledger.h>
#include <app/scheduler.h>

#include <signal.h>

namespace {

// Signals whose default action is to do nothing, so they never interrupt or end an app.
// There is no job control, so the stop signals are discarded like for an orphaned process group,
// and SIGCONT never finds a stopped app.
bool is_ignored_by_default(int sig) {
    switch (sig) {
        case SIGCHLD:
        case SIGWINCH:
        case SIGURG:
        case SIGCONT:
        case SIGTSTP:
        case SIGTTIN:
        case SIGTTOU:
            return true;
        default:
            return false;
    }
}

bool is_ignored(AppLibcSignalHandler handler, int sig) {
    if (handler == SIG_IGN) {
        return true;
    }
    return handler == SIG_DFL && is_ignored_by_default(sig);
}

} // namespace

extern "C" {

error_t app_signal_send(AppInstanceId app_instance_id, int sig) {
    if (sig <= 0 || sig >= APP_LIBC_SIGNAL_COUNT) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (app_manager_get_state(app_instance_id) == APP_INSTANCE_STATE_STOPPED) {
        return ERROR_NOT_FOUND;
    }

    AppEvent event {};
    if (sig == SIGTERM) {
        event.type = APP_EVENT_CLOSE;
    } else {
        event.type = APP_EVENT_SIGNAL;
        event.signal.sig = sig;
    }
    const error_t emit_result = app_event_emit(app_instance_id, &event);
    if (emit_result != ERROR_NOT_FOUND) {
        return emit_result;
    }

    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    AppInstanceRecord& record = iterator->second;
    if (is_ignored(record.signal_handlers[sig], sig)) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NONE;
    }
    record.pending_signals |= 1u << sig;
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

bool app_signal_is_pending(void) {
    const AppInstanceId app_instance_id = app_scheduler_current_app_id();
    if (app_instance_id == 0) {
        return false;
    }
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    const bool pending = iterator != ledger.instances.end() && iterator->second.pending_signals != 0;
    mutex_unlock(&ledger.mutex);
    return pending;
}

void app_signal_deliver_pending(void) {
    const AppInstanceId app_instance_id = app_scheduler_current_app_id();
    if (app_instance_id == 0) {
        return;
    }
    auto& ledger = app_ledger();
    while (true) {
        mutex_lock(&ledger.mutex);
        auto iterator = ledger.instances.find(app_instance_id);
        if (iterator == ledger.instances.end() || iterator->second.pending_signals == 0) {
            mutex_unlock(&ledger.mutex);
            return;
        }
        AppInstanceRecord& record = iterator->second;
        const int sig = __builtin_ctz(record.pending_signals);
        record.pending_signals &= ~(1u << sig);
        // Read at delivery rather than when sent, as the app may have changed it since
        const AppLibcSignalHandler handler = record.signal_handlers[sig];
        mutex_unlock(&ledger.mutex);

        if (is_ignored(handler, sig)) {
            continue;
        }
        if (handler == SIG_DFL) {
            app_scheduler_exit_current(128 + sig);
            return;
        }
        handler(sig);
    }
}

} // extern "C"
