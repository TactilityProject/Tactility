#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <tactility/drivers/esp32_esp_hosted_ota.h>
#include <tactility/drivers/wifi.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>

#include <esp_hosted.h>
extern "C" {
#include <esp_hosted_ota.h>
}
#include <esp_hosted_api_types.h>
#include <esp_hosted_event.h>
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <atomic>
#include <cstring>

#define TAG "esp32_esp_hosted_ota"

namespace {

constexpr EventBits_t CP_INIT_BIT = BIT0;

// An event group (not a binary semaphore) because multiple callers can wait concurrently - a
// binary semaphore only wakes one waiter per give(), which would leave the other(s) blocked
// until a second CP_INIT event that may never come.
EventGroupHandle_t cpInitEventGroup = nullptr;
esp_event_handler_instance_t cpInitHandlerInstance = nullptr;
std::atomic<bool> handlerRegistered{false};

// Monotonically bumped on every CP_INIT (including ones after a slave reset/reboot). Callers
// snapshot this before triggering a (re)connect and wait for it to advance past that snapshot,
// so a CP_INIT_BIT left set from a boot that predates the call can't be mistaken for readiness of
// the *current* RPC layer.
std::atomic<uint32_t> cpInitGeneration{0};

void onCpInitEvent(void* /*arg*/, esp_event_base_t /*base*/, int32_t /*id*/, void* /*data*/) {
    cpInitGeneration.fetch_add(1);
    if (cpInitEventGroup != nullptr) {
        xEventGroupSetBits(cpInitEventGroup, CP_INIT_BIT);
    }
}

/** Thread-safe lazy init: the first caller through wins the race, so concurrent callers can't
 *  both try to create the event group/register the handler at once. */
bool ensureHandlerRegistered() {
    if (handlerRegistered) {
        return true;
    }
    static std::atomic<bool> initializing{false};
    bool expected = false;
    if (!initializing.compare_exchange_strong(expected, true)) {
        while (!handlerRegistered && initializing) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return handlerRegistered;
    }

    if (cpInitEventGroup == nullptr) {
        cpInitEventGroup = xEventGroupCreate();
    }
    if (cpInitEventGroup == nullptr) {
        LOG_E(TAG, "xEventGroupCreate() failed");
        initializing = false;
        return false;
    }
    if (cpInitHandlerInstance == nullptr) {
        esp_err_t err = esp_event_handler_instance_register(ESP_HOSTED_EVENT, ESP_HOSTED_EVENT_CP_INIT,
            onCpInitEvent, nullptr, &cpInitHandlerInstance);
        if (err != ESP_OK) {
            LOG_E(TAG, "esp_event_handler_instance_register() failed: %d", err);
            initializing = false;
            return false;
        }
    }

    handlerRegistered = true;
    initializing = false;
    return true;
}

bool waitReady(void* /*ctx*/, uint32_t timeoutMs) {
    esp_hosted_coprocessor_fwver_t probeVersion = {};
    if (esp_hosted_get_coprocessor_fwversion(&probeVersion) == ESP_OK) {
        // Already up (e.g. WiFi/BT brought it up earlier this boot).
        return true;
    }

    // Register the event handler *before* triggering the connect, so we can't miss the
    // ESP_HOSTED_EVENT_CP_INIT event firing in the window between triggering and waiting.
    if (!ensureHandlerRegistered()) {
        return false;
    }

    // Snapshot the generation before triggering (re)connect - a CP_INIT that already happened
    // (e.g. from a boot before a slave reset) doesn't count as readiness for this call; only a
    // CP_INIT observed after this point (generation advances past the snapshot) does.
    uint32_t generationBeforeConnect = cpInitGeneration.load();

    // Nothing in Tactility's boot sequence calls esp_hosted_connect_to_slave() on its own
    // (only WiFi/BT starting does today, as a side effect) - trigger it ourselves so ESP-NOW
    // and OTA can work standalone, matching how ESP-NOW works on native (non-hosted) chips.
    if (esp_hosted_connect_to_slave() != ESP_OK) {
        LOG_W(TAG, "esp_hosted_connect_to_slave() returned an error - will still wait for CP_INIT in case it's async");
    }

    // Wait for the co-processor's own RPC layer to finish booting (ESP_HOSTED_EVENT_CP_INIT,
    // logged upstream as "Coprocessor Boot-up") - this is later than the transport/SDIO link
    // simply being up, and firing a custom RPC request before this point corrupts the RPC
    // channel for subsequent real calls (observed: esp_wifi_init() failing until reboot).
    // vTaskSetTimeOutState()/xTaskCheckForTimeOut() track elapsed ticks from a snapshot rather
    // than comparing against a fixed deadline tick count, so this is correct across a tick-count
    // wraparound (a fixed "now + timeout" deadline can wrap past TickType_t's max and compare as
    // already-elapsed on the very next check).
    TimeOut_t timeoutState;
    vTaskSetTimeOutState(&timeoutState);
    TickType_t remaining = pdMS_TO_TICKS(timeoutMs);
    while (true) {
        if (cpInitGeneration.load() != generationBeforeConnect) {
            return true;
        }

        if (xTaskCheckForTimeOut(&timeoutState, &remaining) == pdTRUE) {
            return false;
        }

        // pdTRUE: consume the bit so a stale (pre-existing) signal doesn't let a *later* call
        // short-circuit past its own fresh wait. Safe for concurrent waiters because every waiter
        // re-checks cpInitGeneration (not just the bit) both before and after waking - FreeRTOS
        // delivers a set to all currently-blocked waiters before any auto-clear happens, so a
        // waiter still genuinely waiting for a not-yet-arrived CP_INIT simply loops again.
        EventBits_t bits = xEventGroupWaitBits(cpInitEventGroup, CP_INIT_BIT, pdTRUE, pdFALSE, remaining);
        if ((bits & CP_INIT_BIT) == 0) {
            return false; // timed out
        }
        if (cpInitGeneration.load() != generationBeforeConnect) {
            return true; // genuinely new CP_INIT since this call's connect trigger
        }
        // Otherwise: consumed a stale bit - loop back and re-wait.
    }
}

error_t getInfo(void* /*ctx*/, FirmwareInfo* info) {
    if (info == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    esp_hosted_coprocessor_fwver_t version = {};
    esp_err_t err = esp_hosted_get_coprocessor_fwversion(&version);
    if (err != ESP_OK) {
        return esp_err_to_error(err);
    }
    info->fw_major = version.major1;
    info->fw_minor = version.minor1;
    info->fw_patch = version.patch1;

    // Chip identification is best-effort: report the version even if this fails (e.g. a slave
    // firmware old enough to lack the esp_hosted_get_cp_info() RPC).
    info->name[0] = '\0';
    info->hw_id = 0;
    uint32_t chipId = 0;
    if (::esp_hosted_get_cp_info(&chipId, info->name, sizeof(info->name)) == ESP_OK) {
        info->hw_id = chipId;
    }

    return ERROR_NONE;
}

// esp_hosted's OTA API is a single global stream (esp_hosted_slave_ota_begin/write/end/activate
// take no handle/context - there's exactly one co-processor). FirmwareUpdateHandle is an
// intentionally-incomplete/opaque type (see wifi.h) so it can't be instantiated directly - this
// sentinel object just gives begin()/write()/finish()/abort() a distinct, non-null address to
// pass around and check against, matching the generic FirmwareOps shape.
int otaHandleSentinelStorage = 0;
FirmwareUpdateHandle* otaHandleSentinel = reinterpret_cast<FirmwareUpdateHandle*>(&otaHandleSentinelStorage);

// Guards against two overlapping OTA sessions (concurrent begin() calls) and against a stale
// handle from a finished/aborted session still being accepted by write()/finish()/abort() - the
// sentinel address alone can't distinguish "the one active session" from "a session that already
// ended", since it's always the same pointer.
std::atomic<bool> otaSessionActive{false};

error_t beginUpdate(void* /*ctx*/, const FirmwareUpdateRequest* /*req*/, FirmwareUpdateHandle** handle) {
    if (handle == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    bool expected = false;
    if (!otaSessionActive.compare_exchange_strong(expected, true)) {
        return ERROR_RESOURCE_BUSY;
    }
    esp_err_t err = ::esp_hosted_slave_ota_begin();
    if (err != ESP_OK) {
        otaSessionActive = false;
        return esp_err_to_error(err);
    }
    *handle = otaHandleSentinel;
    return ERROR_NONE;
}

error_t writeUpdate(FirmwareUpdateHandle* handle, const void* data, size_t len) {
    if (handle != otaHandleSentinel || !otaSessionActive.load()) {
        return ERROR_INVALID_ARGUMENT;
    }
    return esp_err_to_error(::esp_hosted_slave_ota_write(
        const_cast<uint8_t*>(static_cast<const uint8_t*>(data)), static_cast<uint32_t>(len)));
}

error_t finishUpdate(FirmwareUpdateHandle* handle) {
    if (handle != otaHandleSentinel || !otaSessionActive.load()) {
        return ERROR_INVALID_ARGUMENT;
    }
    otaSessionActive = false;
    return esp_err_to_error(::esp_hosted_slave_ota_end());
}

error_t abortUpdate(FirmwareUpdateHandle* handle) {
    if (handle != otaHandleSentinel || !otaSessionActive.load()) {
        return ERROR_INVALID_ARGUMENT;
    }
    otaSessionActive = false;
    // esp_hosted has no distinct abort call - end() is the only way to close out a begin(),
    // successful or not (matches how the previous single-stream app code always called
    // esp_hosted_slave_ota_end() on both the success and failure paths).
    return esp_err_to_error(::esp_hosted_slave_ota_end());
}

error_t activate(void* /*ctx*/) {
    return esp_err_to_error(::esp_hosted_slave_ota_activate());
}

const FirmwareOps firmwareOps = {
    .wait_ready = waitReady,
    .get_info = getInfo,
    .begin = beginUpdate,
    .write = writeUpdate,
    .finish = finishUpdate,
    .abort = abortUpdate,
    .activate = activate,
};

} // namespace

const FirmwareOps* esp32_esp_hosted_ota_get_ops() {
    return &firmwareOps;
}

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
