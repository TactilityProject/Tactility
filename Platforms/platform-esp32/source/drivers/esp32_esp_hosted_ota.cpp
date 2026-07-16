#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <tactility/drivers/esp_hosted_ota.h>
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

} // namespace

namespace {

bool waitForTransport(uint32_t timeoutMs);
error_t getCoprocessorFwversion(uint32_t* major, uint32_t* minor, uint32_t* patch);
error_t getCpInfo(uint32_t* chipId, char* targetName, size_t targetNameLen);
error_t otaBegin(void);
error_t otaWrite(const uint8_t* data, size_t length);
error_t otaEnd(void);
error_t otaActivate(void);

const EspHostedOtaApi api = {
    .wait_for_transport = waitForTransport,
    .get_coprocessor_fwversion = getCoprocessorFwversion,
    .get_cp_info = getCpInfo,
    .begin = otaBegin,
    .write = otaWrite,
    .end = otaEnd,
    .activate = otaActivate,
};

} // namespace

// Not a Driver/Device (see the header comment for why), so unlike esp32_wifi.cpp etc. there's no
// driver_construct_add() call anywhere to force the linker to pull this translation unit out of
// libplatform-esp32.a - nothing else in the program references any symbol defined here. A
// self-registering static-init object was tried and silently never ran for exactly that reason.
// Instead, module.cpp calls this explicitly (extern declaration there), the same way it directly
// references every other platform-esp32 driver.
extern "C" void esp32_esp_hosted_ota_init() {
    esp_hosted_ota_register(&api);
}

namespace {

bool waitForTransport(uint32_t timeoutMs) {
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
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
    while (true) {
        if (cpInitGeneration.load() != generationBeforeConnect) {
            return true;
        }

        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            return false;
        }
        TickType_t remaining = deadline - now;

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

error_t getCoprocessorFwversion(uint32_t* major, uint32_t* minor, uint32_t* patch) {
    esp_hosted_coprocessor_fwver_t version = {};
    esp_err_t err = esp_hosted_get_coprocessor_fwversion(&version);
    if (err != ESP_OK) {
        return esp_err_to_error(err);
    }
    *major = version.major1;
    *minor = version.minor1;
    *patch = version.patch1;
    return ERROR_NONE;
}

error_t getCpInfo(uint32_t* chipId, char* targetName, size_t targetNameLen) {
    return esp_err_to_error(::esp_hosted_get_cp_info(chipId, targetName, targetNameLen));
}

error_t otaBegin(void) {
    return esp_err_to_error(::esp_hosted_slave_ota_begin());
}

error_t otaWrite(const uint8_t* data, size_t length) {
    return esp_err_to_error(::esp_hosted_slave_ota_write(const_cast<uint8_t*>(data), static_cast<uint32_t>(length)));
}

error_t otaEnd(void) {
    return esp_err_to_error(::esp_hosted_slave_ota_end());
}

error_t otaActivate(void) {
    return esp_err_to_error(::esp_hosted_slave_ota_activate());
}

} // namespace

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
