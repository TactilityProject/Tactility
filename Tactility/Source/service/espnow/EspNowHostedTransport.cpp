#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNowHostedTransport.h>

#include <esp_hosted.h>
#include <esp_hosted_api_types.h>
#include <esp_hosted_event.h>
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <atomic>

#include <tactility/log.h>

namespace tt::service::espnow::backend {

constexpr auto* TAG = "EspNowHostedTransport";
constexpr EventBits_t CP_INIT_BIT = BIT0;

// An event group (not a binary semaphore) because multiple callers can wait concurrently
// (e.g. the chat app's EspNowBackendHosted::init() and the OTA update app's performUpdate() can
// both call waitForHostedTransport() around the same time) - a binary semaphore only wakes one
// waiter per give(), which would leave the other(s) blocked until a second CP_INIT event that
// may never come.
static EventGroupHandle_t cpInitEventGroup = nullptr;
static esp_event_handler_instance_t cpInitHandlerInstance = nullptr;
static std::atomic<bool> handlerRegistered{false};
// Monotonically bumped by onCpInitEvent() on every CP_INIT (including ones after a slave
// reset/reboot). waitForHostedTransport() snapshots this before triggering a (re)connect and
// waits for it to advance past that snapshot, so a CP_INIT_BIT left set from a boot that
// predates this call can't be mistaken for readiness of the *current* RPC layer.
static std::atomic<uint32_t> cpInitGeneration{0};

static void onCpInitEvent(void* /*arg*/, esp_event_base_t /*base*/, int32_t /*id*/, void* /*data*/) {
    cpInitGeneration.fetch_add(1);
    if (cpInitEventGroup != nullptr) {
        xEventGroupSetBits(cpInitEventGroup, CP_INIT_BIT);
    }
}

/** Thread-safe lazy init: the first caller through wins the race (via the atomic exchange
 *  below), so concurrent callers can't both try to create the event group/register the handler
 *  at once. Returns false if setup failed (allocation failure or registration error), so
 *  callers don't wait on a semaphore/handler that was never actually created. */
static bool ensureHandlerRegistered() {
    if (handlerRegistered) {
        return true;
    }
    static std::atomic<bool> initializing{false};
    bool expected = false;
    if (!initializing.compare_exchange_strong(expected, true)) {
        // Another task is already doing setup - wait for it to finish.
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

bool waitForHostedTransport(uint32_t timeoutMs) {
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
    // can work standalone, matching how it works on native (non-hosted) chips.
    if (esp_hosted_connect_to_slave() != ESP_OK) {
        LOG_W(TAG, "esp_hosted_connect_to_slave() returned an error - will still wait for CP_INIT in case it's async");
    }

    // Wait for the co-processor's own RPC layer to finish booting (ESP_HOSTED_EVENT_CP_INIT,
    // logged upstream as "Coprocessor Boot-up") - this is later than the transport/SDIO link
    // simply being up, and firing a custom RPC request before this point corrupts the RPC
    // channel for subsequent real calls (observed: esp_wifi_init() failing until reboot).
    // Polled in a loop against the generation counter (rather than a single wait) because a
    // stale CP_INIT_BIT can be set going in - each failed check clears the bit and waits again so
    // a genuinely new CP_INIT after this call's connect trigger still wakes us.
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
    while (true) {
        // Fast path: another waiter's generation check (or this call's own previous loop
        // iteration) may already have observed a fresh CP_INIT - never clear the bit on the
        // strength of a generation check alone, only xEventGroupWaitBits() below does that, and
        // only after actually consuming a signal.
        if (cpInitGeneration.load() != generationBeforeConnect) {
            return true;
        }

        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            return false;
        }
        TickType_t remaining = deadline - now;

        // pdTRUE: consume the bit so a stale (pre-existing) signal doesn't let a *later* call
        // short-circuit past its own fresh wait. Safe for concurrent waiters here specifically
        // because every waiter re-checks cpInitGeneration (not just the bit) both before and
        // after waking, so a waiter that's still genuinely waiting for a not-yet-arrived CP_INIT
        // simply loops again - it never depends on observing the bit itself.
        EventBits_t bits = xEventGroupWaitBits(cpInitEventGroup, CP_INIT_BIT,
            pdTRUE, pdFALSE, remaining);
        if ((bits & CP_INIT_BIT) == 0) {
            return false; // timed out
        }
        if (cpInitGeneration.load() != generationBeforeConnect) {
            return true; // genuinely new CP_INIT since this call's connect trigger
        }
        // Otherwise: consumed a stale bit: loop back to fast-path check / re-wait.
    }
}

}

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
