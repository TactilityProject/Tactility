#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)

#include <esp_wifi.h> // for WIFI_TASK_CORE_ID

#include <tactility/concurrent/dispatcher.h>
#include <tactility/concurrent/event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_wifi_pinned.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <cstring>
#include <functional>
#include <new>

#define TAG "esp32_wifi_pinned"

namespace {

constexpr uint32_t CALL_DONE_FLAG = 1U;
constexpr configSTACK_DEPTH_TYPE PINNED_THREAD_STACK_SIZE = 4096;

// This driver wraps "espressif,esp32-wifi" (created as a child device) and
// marshals every call that mutates WiFi state onto a dedicated task pinned to
// WIFI_TASK_CORE_ID. ESP-IDF's WiFi driver misbehaves (hangs waiting on its
// internal task semaphore, tripping the watchdog) when those calls originate
// from an arbitrary caller task/core, so this driver guarantees the correct
// core regardless of who calls the WifiApi.
struct Esp32WifiPinnedCtx {
    Device* device = nullptr;
    Device* child = nullptr;
    Thread* thread = nullptr;
    DispatcherHandle_t dispatcher = nullptr;
    volatile bool running = false;
};

#define GET_CTX(device) (static_cast<Esp32WifiPinnedCtx*>(device_get_driver_data(device)))
// Only for event_subscribe()/event_unsubscribe(): unlike the rest of WifiApi, have
// construct/destruct side effects on sub->ring_mutex at the wifi_event_subscribe()/
// wifi_event_unsubscribe() kernel-wrapper layer (see wifi.cpp) - calling that wrapper AGAIN here
// (instead of the child driver's raw vtable entry) double-runs those side effects, corrupting the
// mutex. Every other WifiApi call below is a plain pass-through.
#define CHILD_WIFI_API(child) ((const struct WifiApi*)device_get_driver(child)->api)

struct MarshalledCall {
    const std::function<void()>* work;
    EventGroupHandle_t done;
};

void marshal_trampoline(void* context) {
    auto* call = static_cast<MarshalledCall*>(context);
    (*call->work)();
    event_group_set(call->done, CALL_DONE_FLAG);
}

/** Runs `work` synchronously on the pinned WiFi task, blocking the caller until it completes. */
error_t run_on_pinned_thread(Esp32WifiPinnedCtx* ctx, const std::function<void()>& work) {
    if (ctx == nullptr || ctx->thread == nullptr) {
        return ERROR_INVALID_STATE;
    }

    // Avoid deadlocking when already running on the pinned thread (e.g. during device_start()).
    if (thread_get_current() == ctx->thread) {
        work();
        return ERROR_NONE;
    }

    MarshalledCall call = { .work = &work, .done = nullptr };
    event_group_construct(&call.done);

    error_t err = dispatcher_dispatch(ctx->dispatcher, &call, marshal_trampoline);
    if (err != ERROR_NONE) {
        event_group_destruct(&call.done);
        return err;
    }

    event_group_wait(call.done, CALL_DONE_FLAG, false, true, nullptr, portMAX_DELAY);
    event_group_destruct(&call.done);
    return ERROR_NONE;
}

void request_stop(void* context) {
    static_cast<Esp32WifiPinnedCtx*>(context)->running = false;
}

int32_t pinned_thread_main(void* context) {
    auto* ctx = static_cast<Esp32WifiPinnedCtx*>(context);
    while (ctx->running) {
        dispatcher_consume(ctx->dispatcher);
    }
    return 0;
}

// ---- Work that must run on the pinned thread (see start_device/stop_device) ----

error_t start_child_work(Esp32WifiPinnedCtx* ctx) {
    ctx->child = new(std::nothrow) Device();
    if (ctx->child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    std::memset(ctx->child, 0, sizeof(Device));
    ctx->child->parent = ctx->device;
    ctx->child->name = ctx->device->name;

    error_t err = device_construct_add_start(ctx->child, "espressif,esp32-wifi");
    if (err != ERROR_NONE) {
        LOG_E(TAG, "%s: failed to start child wifi device: %s", ctx->device->name, error_to_string(err));
        delete ctx->child;
        ctx->child = nullptr;
    }
    return err;
}

void stop_child_work(Esp32WifiPinnedCtx* ctx) {
    if (ctx->child == nullptr) {
        return;
    }
    if (device_is_ready(ctx->child)) {
        device_stop(ctx->child);
    }
    if (device_is_added(ctx->child)) {
        device_remove(ctx->child);
    }
    device_destruct(ctx->child);
    delete ctx->child;
    ctx->child = nullptr;
}

// ---- WifiApi: read-only calls forward straight to the child, no core affinity needed ----

error_t api_get_radio_state(Device* device, WifiRadioState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_get_radio_state(ctx->child, state);
}

error_t api_get_station_state(Device* device, WifiStationState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_get_station_state(ctx->child, state);
}

error_t api_get_access_point_state(Device* device, WifiAccessPointState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_get_access_point_state(ctx->child, state);
}

bool api_is_scanning(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return false;
    return wifi_is_scanning(ctx->child);
}

error_t api_get_scan_results(Device* device, WifiApRecord* results, size_t* num_results) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_get_scan_results(ctx->child, results, num_results);
}

error_t api_station_get_ipv4_address(Device* device, char* ipv4) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_station_get_ipv4_address(ctx->child, ipv4);
}

error_t api_station_get_target_ssid(Device* device, char* ssid) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_station_get_target_ssid(ctx->child, ssid);
}

error_t api_station_get_rssi(Device* device, int32_t* rssi) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return wifi_station_get_rssi(ctx->child, rssi);
}

error_t api_event_subscribe(Device* device, WifiEventSubscription* sub, TaskEventGroup* event_group) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return CHILD_WIFI_API(ctx->child)->event_subscribe(ctx->child, sub, event_group);
}

error_t api_event_unsubscribe(Device* device, WifiEventSubscription* sub) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;
    return CHILD_WIFI_API(ctx->child)->event_unsubscribe(ctx->child, sub);
}

// ---- WifiApi: state-changing calls are marshalled onto the pinned WiFi task ----

error_t api_set_radio_on(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;

    error_t result = ERROR_NONE;
    Device* child = ctx->child;
    error_t err = run_on_pinned_thread(ctx, [child, &result]() {
        result = wifi_set_radio_on(child);
    });
    return err != ERROR_NONE ? err : result;
}

error_t api_set_radio_off(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;

    error_t result = ERROR_NONE;
    Device* child = ctx->child;
    error_t err = run_on_pinned_thread(ctx, [child, &result]() {
        result = wifi_set_radio_off(child);
    });
    return err != ERROR_NONE ? err : result;
}

error_t api_scan(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;

    error_t result = ERROR_NONE;
    Device* child = ctx->child;
    error_t err = run_on_pinned_thread(ctx, [child, &result]() {
        result = wifi_scan(child);
    });
    return err != ERROR_NONE ? err : result;
}

error_t api_station_connect(Device* device, const char* ssid, const char* password, int32_t channel) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;

    error_t result = ERROR_NONE;
    Device* child = ctx->child;
    error_t err = run_on_pinned_thread(ctx, [child, ssid, password, channel, &result]() {
        result = wifi_station_connect(child, ssid, password, channel);
    });
    return err != ERROR_NONE ? err : result;
}

error_t api_station_disconnect(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ctx->child == nullptr) return ERROR_INVALID_STATE;

    error_t result = ERROR_NONE;
    Device* child = ctx->child;
    error_t err = run_on_pinned_thread(ctx, [child, &result]() {
        result = wifi_station_disconnect(child);
    });
    return err != ERROR_NONE ? err : result;
}

const WifiApi esp32_wifi_pinned_api = {
    .set_radio_on = api_set_radio_on,
    .set_radio_off = api_set_radio_off,
    .get_radio_state = api_get_radio_state,
    .get_station_state = api_get_station_state,
    .get_access_point_state = api_get_access_point_state,
    .is_scanning = api_is_scanning,
    .scan = api_scan,
    .get_scan_results = api_get_scan_results,
    .station_get_ipv4_address = api_station_get_ipv4_address,
    .station_get_target_ssid = api_station_get_target_ssid,
    .station_connect = api_station_connect,
    .station_disconnect = api_station_disconnect,
    .station_get_rssi = api_station_get_rssi,
    .event_subscribe = api_event_subscribe,
    .event_unsubscribe = api_event_unsubscribe,
    .get_firmware_ops = nullptr
};

// ---- Driver lifecycle ----
// Starting/stopping the child (allocating/freeing its bookkeeping - see esp32_wifi.cpp's own
// start_device()/stop_device() comment) happens on the pinned thread for the same core-affinity
// reason as the state-changing WifiApi calls above; enable()/disable() (which actually bring the
// ESP-IDF WiFi stack up/down) are marshalled the same way.

error_t start_device(Device* device) {
    auto* ctx = new(std::nothrow) Esp32WifiPinnedCtx();
    if (ctx == nullptr) return ERROR_OUT_OF_MEMORY;
    ctx->device = device;
    device_set_driver_data(device, ctx);

    ctx->dispatcher = dispatcher_alloc();
    ctx->running = true;
    ctx->thread = thread_alloc_full(
        "esp32_wifi_pinned",
        PINNED_THREAD_STACK_SIZE,
        pinned_thread_main,
        ctx,
        WIFI_TASK_CORE_ID
    );
    if (ctx->thread == nullptr || thread_start(ctx->thread) != ERROR_NONE) {
        LOG_E(TAG, "%s: failed to start pinned wifi task", device->name);
        if (ctx->thread != nullptr) thread_free(ctx->thread);
        dispatcher_free(ctx->dispatcher);
        device_set_driver_data(device, nullptr);
        delete ctx;
        return ERROR_RESOURCE;
    }

    error_t result = ERROR_NONE;
    error_t err = run_on_pinned_thread(ctx, [ctx, &result]() {
        result = start_child_work(ctx);
    });
    if (err == ERROR_NONE) {
        err = result;
    }

    if (err != ERROR_NONE) {
        dispatcher_dispatch(ctx->dispatcher, ctx, request_stop);
        thread_join(ctx->thread, portMAX_DELAY, 10);
        thread_free(ctx->thread);
        dispatcher_free(ctx->dispatcher);
        device_set_driver_data(device, nullptr);
        delete ctx;
        return err;
    }

    return ERROR_NONE;
}

error_t stop_device(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    error_t err = run_on_pinned_thread(ctx, [ctx]() {
        stop_child_work(ctx);
    });
    if (err != ERROR_NONE) {
        LOG_E(TAG, "%s: failed to stop child wifi device on pinned task: %s", device->name, error_to_string(err));
    }

    dispatcher_dispatch(ctx->dispatcher, ctx, request_stop);
    thread_join(ctx->thread, portMAX_DELAY, 10);
    thread_free(ctx->thread);
    dispatcher_free(ctx->dispatcher);

    device_set_driver_data(device, nullptr);
    delete ctx;
    return err;
}

} // namespace

extern "C" {

extern Module platform_esp32_module;

Driver esp32_wifi_pinned_driver = {
    .name = "esp32_wifi_pinned",
    .compatible = (const char*[]) { "espressif,esp32-wifi-pinned", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = (const void*)&esp32_wifi_pinned_api,
    .device_type = &WIFI_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"

#endif // CONFIG_SOC_WIFI_SUPPORTED or CONFIG_ESP_HOSTED_ENABLED
