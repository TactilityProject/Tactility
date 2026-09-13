// SPDX-License-Identifier: Apache-2.0
#include <http/download.h>
#include <http/private/download.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>

#include <new>
#include <string>

namespace {

constexpr auto* TAG = "http-download";
constexpr size_t DOWNLOAD_TASK_STACK_DEPTH = 5120;

struct DownloadContext {
    std::string url;
    std::string certPath;
    std::string targetPath;
    HttpDownloadSubscription* subscription;
    HttpDownloadLink* link;
};

// Delivers `event` to `sub` if the subscriber hasn't unsubscribed in the meantime, then releases
// this task's share of `link`, deleting it if the subscriber already released theirs.
void finish(HttpDownloadLink* link, HttpDownloadSubscription* sub, const HttpDownloadEvent& event) {
    mutex_lock(&link->mutex);
    if (link->subscribed) {
        sub->internal.event = event;
        sub->internal.pending = true;
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    bool last = (--link->owners == 0);
    mutex_unlock(&link->mutex);

    if (last) {
        delete link;
    }
}

void download_task_main(void* raw_context) {
    auto* context = static_cast<DownloadContext*>(raw_context);
    LOG_I(TAG, "Downloading %s to %s", context->url.c_str(), context->targetPath.c_str());

    auto event = http_download_run(context->url, context->certPath, context->targetPath, context->link);
    if (event.type == HTTP_DOWNLOAD_EVENT_ERROR) {
        LOG_E(TAG, "Download of %s failed: %s", context->url.c_str(), event.error.message);
    } else if (event.type == HTTP_DOWNLOAD_EVENT_CANCELLED) {
        LOG_I(TAG, "Download of %s cancelled", context->url.c_str());
    } else {
        LOG_I(TAG, "Downloaded %s to %s", context->url.c_str(), context->targetPath.c_str());
    }

    finish(context->link, context->subscription, event);
    delete context;
    vTaskDelete(nullptr);
}

}

extern "C" {

error_t http_download_subscribe(HttpDownloadSubscription* sub, TaskEventGroup* event_group) {
    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    auto* link = new (std::nothrow) HttpDownloadLink();
    if (link == nullptr) {
        task_event_group_release_bit(event_group, bit);
        return ERROR_OUT_OF_MEMORY;
    }

    sub->bit = bit;
    sub->internal.event_group = event_group;
    sub->internal.pending = false;
    sub->internal.link = link;

    return ERROR_NONE;
}

error_t http_download_unsubscribe(HttpDownloadSubscription* sub) {
    HttpDownloadLink* link = sub->internal.link;
    if (link == nullptr) {
        return ERROR_NOT_FOUND;
    }
    // Caller-exclusive: the task never reads this field, so it's safe to clear without the lock.
    sub->internal.link = nullptr;

    TaskEventGroup* event_group;
    uint32_t bit;

    mutex_lock(&link->mutex);
    // Read/clear under the lock: finish() reads these same fields under that lock too,
    // while `subscribed` is true, so this must not race it.
    event_group = sub->internal.event_group;
    bit = sub->bit;
    sub->internal.event_group = nullptr;
    link->subscribed = false;
    bool last = (--link->owners == 0);
    mutex_unlock(&link->mutex);

    // Safe outside the lock: the critical section above already decided whether finish() will
    // ever signal this bit, so nothing will touch it again from here on.
    task_event_group_release_bit(event_group, bit);

    if (last) {
        delete link;
    }

    return ERROR_NONE;
}

error_t http_download_poll(HttpDownloadSubscription* sub, HttpDownloadEvent* out_event) {
    HttpDownloadLink* link = sub->internal.link;
    if (link == nullptr) {
        return ERROR_TIMEOUT;
    }

    mutex_lock(&link->mutex);
    bool pending = sub->internal.pending;
    if (pending) {
        *out_event = sub->internal.event;
        sub->internal.pending = false;
    }
    mutex_unlock(&link->mutex);

    return pending ? ERROR_NONE : ERROR_TIMEOUT;
}

error_t http_download_start(
    const char* url,
    const char* cert_path,
    const char* target_path,
    HttpDownloadSubscription* sub
) {
    HttpDownloadLink* link = sub->internal.link;
    if (link == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* context = new(std::nothrow) DownloadContext {
        .url = url,
        .certPath = cert_path,
        .targetPath = target_path,
        .subscription = sub,
        .link = link
    };
    if (context == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    mutex_lock(&link->mutex);
    link->owners++; // the task's own share, released by finish()
    mutex_unlock(&link->mutex);

    TaskHandle_t task_handle;
    if (xTaskCreate(download_task_main, "http-download", DOWNLOAD_TASK_STACK_DEPTH, context, tskIDLE_PRIORITY + 1, &task_handle) != pdPASS) {
        delete context;

        mutex_lock(&link->mutex);
        bool last = (--link->owners == 0);
        mutex_unlock(&link->mutex);
        if (last) {
            delete link;
        }

        return ERROR_OUT_OF_MEMORY;
    }

    return ERROR_NONE;
}

error_t http_download_cancel(HttpDownloadSubscription* sub) {
    HttpDownloadLink* link = sub->internal.link;
    if (link == nullptr) {
        return ERROR_INVALID_STATE;
    }

    mutex_lock(&link->mutex);
    link->cancel_requested = true;
    mutex_unlock(&link->mutex);

    return ERROR_NONE;
}

}
