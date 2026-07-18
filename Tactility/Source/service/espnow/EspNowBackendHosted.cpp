#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNowBackend.h>
#include <Tactility/service/espnow/EspNowHostedTransport.h>
#include <Tactility/service/espnow/esp_hosted_espnow_bridge_proto.h>

// esp_hosted_misc.h lacks its own extern "C" guard, so its declarations get C++ name-mangled
// when included from a .cpp file, causing "undefined reference" at link time against the
// library's plain-C symbols (esp_hosted_send_custom_data/register_custom_callback).
extern "C" {
#include <esp_hosted_misc.h>
}
#include <esp_now.h>

#include <Tactility/Semaphore.h>
#include <Tactility/RecursiveMutex.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <tactility/log.h>

namespace tt::service::espnow::backend {

constexpr auto* TAG = "EspNowBackendHosted";
constexpr TickType_t REQUEST_TIMEOUT = 2000U / portTICK_PERIOD_MS;

static RecursiveMutex requestMutex;
static Semaphore responseSemaphore(1, 0);
// Callbacks (onRespGeneric/onRespInit) run from esp_hosted's own dispatch task, concurrently
// with doRequest() running on the caller's task - std::atomic instead of requestMutex here
// because taking requestMutex from the callback would deadlock: doRequest() holds it for its
// entire wait, including while blocked in responseSemaphore.acquire() waiting on the very
// callback that would need the lock.
static std::atomic<esp_err_t> lastResponseErr{ESP_FAIL};
static std::atomic<bool> waitingForResponse{false};
// The txn_id doRequest() is currently waiting a response for. Response handlers only accept a
// response (release the semaphore) if its txn_id matches - otherwise a delayed response to a
// prior, already-timed-out request (e.g. a REQ_INIT retry's earlier attempt) can't be mistaken
// for the answer to whatever request is active now, even a later request of the same type.
static std::atomic<uint32_t> activeTxnId{0};
static std::atomic<uint32_t> nextTxnId{1};

// Written by init()/deinit() on the caller's task, read by onEvtRecv() on esp_hosted's dispatch
// task - atomic so a callback removal during deinit() can't race a concurrent RX event dispatch
// (the dispatch task must see either the old callback or nullptr, never a torn/stale pointer).
static std::atomic<esp_now_recv_cb_t> userRecvCallback{nullptr};
static uint32_t espnowVersion = 0;

static std::atomic<uint32_t> lastReportedInitVersion{0};

static void onRespGeneric(uint32_t msgId, const uint8_t* data, size_t dataLen, void* ctx) {
    (void)msgId; (void)ctx;
    if (dataLen != sizeof(espnow_bridge_resp_status_t)) {
        LOG_E(TAG, "Malformed response (%zu bytes)", dataLen);
        return;
    }
    const auto* resp = reinterpret_cast<const espnow_bridge_resp_status_t*>(data);
    if (!waitingForResponse || resp->txn_id != activeTxnId.load()) {
        return; // stale response for a request that's no longer (or never was) being waited on
    }
    lastResponseErr = static_cast<esp_err_t>(resp->esp_err);
    responseSemaphore.release();
}

static void onRespInit(uint32_t msgId, const uint8_t* data, size_t dataLen, void* ctx) {
    (void)msgId; (void)ctx;
    if (dataLen != sizeof(espnow_bridge_resp_init_t)) {
        LOG_E(TAG, "Malformed RESP_INIT (%zu bytes)", dataLen);
        return;
    }
    const auto* resp = reinterpret_cast<const espnow_bridge_resp_init_t*>(data);
    if (!waitingForResponse || resp->txn_id != activeTxnId.load()) {
        return; // stale response for a request that's no longer (or never was) being waited on
    }
    lastResponseErr = static_cast<esp_err_t>(resp->esp_err);
    lastReportedInitVersion = resp->espnow_version;
    responseSemaphore.release();
}

static void onEvtRecv(uint32_t msgId, const uint8_t* data, size_t dataLen, void* ctx) {
    (void)msgId; (void)ctx;
    if (dataLen != sizeof(espnow_bridge_evt_recv_t)) {
        LOG_E(TAG, "Malformed RX event (%zu bytes)", dataLen);
        return;
    }
    esp_now_recv_cb_t recvCallback = userRecvCallback.load();
    if (recvCallback == nullptr) {
        return;
    }

    const auto* evt = reinterpret_cast<const espnow_bridge_evt_recv_t*>(data);
    if (evt->data_len > ESPNOW_BRIDGE_MAX_DATA_LEN) {
        LOG_E(TAG, "Malformed RX event: data_len %u exceeds buffer capacity", (unsigned)evt->data_len);
        return;
    }

    // Build a transient esp_now_recv_info_t matching the shape native ESP-NOW delivers.
    wifi_pkt_rx_ctrl_t rxCtrl = {};
    rxCtrl.rssi = evt->rssi;
    rxCtrl.channel = evt->channel;

    uint8_t srcAddr[ESPNOW_BRIDGE_ETH_ALEN];
    uint8_t desAddr[ESPNOW_BRIDGE_ETH_ALEN];
    memcpy(srcAddr, evt->src_addr, ESPNOW_BRIDGE_ETH_ALEN);
    memcpy(desAddr, evt->des_addr, ESPNOW_BRIDGE_ETH_ALEN);

    esp_now_recv_info_t recvInfo = {};
    recvInfo.src_addr = srcAddr;
    recvInfo.des_addr = desAddr;
    recvInfo.rx_ctrl = &rxCtrl;

    recvCallback(&recvInfo, evt->data, evt->data_len);
}

static void onEvtSendStatus(uint32_t msgId, const uint8_t* data, size_t dataLen, void* ctx) {
    (void)msgId; (void)ctx;
    if (dataLen != sizeof(espnow_bridge_evt_send_status_t)) {
        LOG_E(TAG, "Malformed send-status event (%zu bytes)", dataLen);
        return;
    }
    const auto* evt = reinterpret_cast<const espnow_bridge_evt_send_status_t*>(data);
    if (!evt->success) {
        LOG_W(TAG, "Peer send reported failure");
    }
}

/** Sends a request and blocks (holding requestMutex) until the matching response arrives or
 *  times out. Every request struct's first field is `uint32_t txn_id`; doRequest() stamps a
 *  fresh transaction id directly into that leading 4 bytes of reqData (memcpy rather than a
 *  `uint32_t*` into the packed struct, which trips -Waddress-of-packed-member even though the
 *  first field of every one of these structs is always naturally aligned) before sending - the
 *  matching response handler (onRespGeneric/onRespInit) only accepts a response whose txn_id
 *  equals this call's, so a late response to an earlier, already-timed-out request (e.g. a
 *  REQ_INIT retry) can't be mistaken for the answer to this request. */
static esp_err_t doRequest(uint32_t reqMsgId, uint8_t* reqData, size_t reqDataLen) {
    auto lock = requestMutex.asScopedLock();
    lock.lock();

    // Drain any stale token left by a response that arrived after a previous request's
    // doRequest() call already timed out - without this, that late release would let this new
    // request's acquire() below return immediately with a stale lastResponseErr/version instead
    // of actually waiting for its own response.
    while (responseSemaphore.acquire(0)) {}

    uint32_t txnId = nextTxnId.fetch_add(1);
    memcpy(reqData, &txnId, sizeof(txnId));
    activeTxnId = txnId;

    waitingForResponse = true;
    esp_err_t sendErr = esp_hosted_send_custom_data(reqMsgId, reqData, reqDataLen);
    if (sendErr != ESP_OK) {
        waitingForResponse = false;
        LOG_E(TAG, "esp_hosted_send_custom_data() failed for req 0x%lx: %d", (unsigned long)reqMsgId, sendErr);
        return sendErr;
    }

    bool gotResponse = responseSemaphore.acquire(REQUEST_TIMEOUT);
    waitingForResponse = false;
    if (!gotResponse) {
        LOG_E(TAG, "Timed out waiting for response to req 0x%lx", (unsigned long)reqMsgId);
        return ESP_ERR_TIMEOUT;
    }

    return lastResponseErr;
}

static bool registerCallbacksOnce() {
    static bool registered = false;
    if (registered) {
        return true;
    }

    bool allOk = true;
    allOk &= esp_hosted_register_custom_callback(ESPNOW_BRIDGE_RESP_INIT, onRespInit, nullptr) == ESP_OK;
    allOk &= esp_hosted_register_custom_callback(ESPNOW_BRIDGE_RESP_DEINIT, onRespGeneric, nullptr) == ESP_OK;
    allOk &= esp_hosted_register_custom_callback(ESPNOW_BRIDGE_RESP_ADD_PEER, onRespGeneric, nullptr) == ESP_OK;
    allOk &= esp_hosted_register_custom_callback(ESPNOW_BRIDGE_RESP_SEND, onRespGeneric, nullptr) == ESP_OK;
    allOk &= esp_hosted_register_custom_callback(ESPNOW_BRIDGE_EVT_RECV, onEvtRecv, nullptr) == ESP_OK;
    allOk &= esp_hosted_register_custom_callback(ESPNOW_BRIDGE_EVT_SEND_STATUS, onEvtSendStatus, nullptr) == ESP_OK;

    if (!allOk) {
        LOG_E(TAG, "Failed to register one or more custom callbacks");
        return false;
    }

    registered = true;
    return true;
}

bool init(const EspNowConfig& config, esp_now_recv_cb_t recvCallback) {
    constexpr uint32_t HOSTED_TRANSPORT_WAIT_TIMEOUT_MS = 10000;
    if (!waitForHostedTransport(HOSTED_TRANSPORT_WAIT_TIMEOUT_MS)) {
        LOG_E(TAG, "esp_hosted transport didn't come up within %" PRIu32 "ms - is WiFi enabled?",
            HOSTED_TRANSPORT_WAIT_TIMEOUT_MS);
        return false;
    }

    if (!registerCallbacksOnce()) {
        return false;
    }

    userRecvCallback = recvCallback;

    espnow_bridge_req_init_t req = {};
    memcpy(req.pmk, config.masterKey, ESPNOW_BRIDGE_KEY_LEN);
    req.channel = config.channel;
    req.long_range = config.longRange;
    req.mode = (config.mode == Mode::AccessPoint) ? ESPNOW_BRIDGE_MODE_ACCESS_POINT : ESPNOW_BRIDGE_MODE_STATION;

    // The slave registers its custom RPC handlers (slave_espnow_bridge_init()) partway through
    // its own app_main(), which isn't gated on - and can run later than - the CP_INIT event
    // waitForHostedTransport() already waited for. So the very first REQ_INIT after a cold
    // boot can still race ahead of the slave being ready to handle it, timing out once even
    // though the transport itself is fine. Retry a few times with a short backoff before
    // giving up - the slave finishes registering shortly after CP_INIT in practice.
    constexpr int MAX_INIT_ATTEMPTS = 4;
    constexpr TickType_t INIT_RETRY_DELAY = 500U / portTICK_PERIOD_MS;

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= MAX_INIT_ATTEMPTS; attempt++) {
        err = doRequest(ESPNOW_BRIDGE_REQ_INIT, reinterpret_cast<uint8_t*>(&req), sizeof(req));
        if (err == ESP_OK) {
            break;
        }
        if (attempt < MAX_INIT_ATTEMPTS) {
            LOG_W(TAG, "REQ_INIT attempt %d/%d failed (%d) - retrying, slave may still be booting",
                attempt, MAX_INIT_ATTEMPTS, err);
            vTaskDelay(INIT_RETRY_DELAY);
        }
    }

    if (err != ESP_OK) {
        LOG_E(TAG, "Remote esp_now_init() failed after %d attempts: %d", MAX_INIT_ATTEMPTS, err);
        userRecvCallback = nullptr;
        return false;
    }

    espnowVersion = lastReportedInitVersion;
    if (espnowVersion != 0) {
        LOG_I(TAG, "Co-processor ESP-NOW version: %u.0", (unsigned)espnowVersion);
    } else {
        LOG_W(TAG, "Co-processor didn't report an ESP-NOW version - assuming v1.0");
        espnowVersion = 1;
    }
    return true;
}

bool deinit() {
    espnow_bridge_req_deinit_t req = {};
    esp_err_t err = doRequest(ESPNOW_BRIDGE_REQ_DEINIT, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    userRecvCallback = nullptr;
    espnowVersion = 0;
    if (err != ESP_OK) {
        LOG_E(TAG, "Remote esp_now_deinit() failed: %d", err);
        return false;
    }
    return true;
}

bool addPeer(const esp_now_peer_info_t& peer) {
    espnow_bridge_req_add_peer_t req = {};
    memcpy(req.peer_addr, peer.peer_addr, ESPNOW_BRIDGE_ETH_ALEN);
    memcpy(req.lmk, peer.lmk, ESPNOW_BRIDGE_KEY_LEN);
    req.channel = peer.channel;
    req.encrypt = peer.encrypt;
    req.ifidx = (peer.ifidx == WIFI_IF_AP) ? ESPNOW_BRIDGE_MODE_ACCESS_POINT : ESPNOW_BRIDGE_MODE_STATION;

    esp_err_t err = doRequest(ESPNOW_BRIDGE_REQ_ADD_PEER, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    if (err != ESP_OK) {
        LOG_E(TAG, "Remote esp_now_add_peer() failed: %d", err);
        return false;
    }
    return true;
}

bool send(const uint8_t* address, const uint8_t* buffer, size_t bufferLength) {
    if (bufferLength > ESPNOW_BRIDGE_MAX_DATA_LEN) {
        LOG_E(TAG, "Payload too large for bridge (%zu bytes)", bufferLength);
        return false;
    }

    espnow_bridge_req_send_t req = {};
    req.broadcast = (address == nullptr);
    if (!req.broadcast) {
        memcpy(req.dest_addr, address, ESPNOW_BRIDGE_ETH_ALEN);
    }
    req.data_len = static_cast<uint16_t>(bufferLength);
    memcpy(req.data, buffer, bufferLength);

    // Must send the full fixed-size struct, not just header + payload: the slave's on_req_send
    // validates data_len against sizeof(espnow_bridge_req_send_t) exactly, so a truncated wire
    // length was always rejected as ESP_ERR_INVALID_SIZE (this made every send fail while
    // receive still worked fine, since RX events aren't size-truncated the same way).
    esp_err_t err = doRequest(ESPNOW_BRIDGE_REQ_SEND, reinterpret_cast<uint8_t*>(&req), sizeof(req));
    return err == ESP_OK;
}

uint32_t getVersion() {
    return espnowVersion;
}

}

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
