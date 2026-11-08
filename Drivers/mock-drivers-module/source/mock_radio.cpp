// SPDX-License-Identifier: Apache-2.0

// Mock packet radio for the simulator. All started mock radios share an in-process ether:
// a transmission is delivered right away to every other mock radio that is on and tuned to the
// same modulation, frequency, sync word, spreading factor and bandwidth.

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/radio.h>
#include <tactility/log.h>

#include <new>

#define TAG "mock_radio"

namespace {

constexpr int PARAMETER_COUNT = RADIO_PARAMETER_CHANNEL + 1;
constexpr float MOCK_RX_RSSI = -60.0f;
constexpr float MOCK_RX_SNR = 9.5f;
constexpr float MOCK_NOISE_FLOOR = -120.0f;

struct MockRadioCtx {
    Device* device = nullptr;
    RadioState state = RADIO_STATE_OFF;
    RadioModulation modulation = RADIO_MODULATION_NONE;
    int32_t parameters[PARAMETER_COUNT] = {};
    RadioTxId nextTxId = 0;
    MockRadioCtx* next = nullptr;
};

struct EtherMutex {
    Mutex handle {};
    EtherMutex() { mutex_construct(&handle); }
    ~EtherMutex() { mutex_destruct(&handle); }
};

// Guards the ether list and the state of every mock radio on it
EtherMutex etherMutex;
MockRadioCtx* ether = nullptr;

#define GET_CTX(device) (static_cast<MockRadioCtx*>(device_get_driver_data(device)))

bool isTunedAlike(const MockRadioCtx* a, const MockRadioCtx* b) {
    constexpr RadioParameter matching[] = {
        RADIO_PARAMETER_FREQUENCY,
        RADIO_PARAMETER_SYNC_WORD,
        RADIO_PARAMETER_SPREADING_FACTOR,
        RADIO_PARAMETER_BANDWIDTH,
    };
    if (a->modulation != b->modulation) {
        return false;
    }
    for (auto parameter : matching) {
        if (a->parameters[parameter] != b->parameters[parameter]) {
            return false;
        }
    }
    return true;
}

void setState(MockRadioCtx* ctx, RadioState state) {
    ctx->state = state;
    radio_state_event_emit(ctx->device, state);
}

error_t apiGetRadioState(Device* device, RadioState* state) {
    mutex_lock(&etherMutex.handle);
    *state = GET_CTX(device)->state;
    mutex_unlock(&etherMutex.handle);
    return ERROR_NONE;
}

error_t apiSetEnabled(Device* device, bool enabled) {
    auto* ctx = GET_CTX(device);
    mutex_lock(&etherMutex.handle);
    error_t result = ERROR_NONE;
    if (enabled) {
        if (ctx->modulation == RADIO_MODULATION_NONE) {
            result = ERROR_INVALID_STATE;
        } else if (ctx->state != RADIO_STATE_ON) {
            setState(ctx, RADIO_STATE_ON_PENDING);
            setState(ctx, RADIO_STATE_ON);
        }
    } else if (ctx->state != RADIO_STATE_OFF) {
        setState(ctx, RADIO_STATE_OFF_PENDING);
        setState(ctx, RADIO_STATE_OFF);
    }
    mutex_unlock(&etherMutex.handle);
    return result;
}

error_t apiSetModulation(Device* device, RadioModulation modulation) {
    auto* ctx = GET_CTX(device);
    mutex_lock(&etherMutex.handle);
    error_t result = ERROR_NONE;
    if (ctx->state == RADIO_STATE_ON || ctx->state == RADIO_STATE_ON_PENDING) {
        result = ERROR_INVALID_STATE;
    } else {
        ctx->modulation = modulation;
    }
    mutex_unlock(&etherMutex.handle);
    return result;
}

error_t apiGetModulation(Device* device, RadioModulation* modulation) {
    mutex_lock(&etherMutex.handle);
    *modulation = GET_CTX(device)->modulation;
    mutex_unlock(&etherMutex.handle);
    return ERROR_NONE;
}

bool apiCanTransmitOrReceive(Device*, RadioModulation modulation) {
    return modulation != RADIO_MODULATION_NONE;
}

error_t apiSetParameter(Device* device, RadioParameter parameter, int32_t value) {
    if (parameter < 0 || parameter >= PARAMETER_COUNT) {
        return ERROR_NOT_SUPPORTED;
    }
    mutex_lock(&etherMutex.handle);
    GET_CTX(device)->parameters[parameter] = value;
    mutex_unlock(&etherMutex.handle);
    return ERROR_NONE;
}

error_t apiGetParameter(Device* device, RadioParameter parameter, int32_t* value) {
    if (parameter < 0 || parameter >= PARAMETER_COUNT) {
        return ERROR_NOT_SUPPORTED;
    }
    mutex_lock(&etherMutex.handle);
    *value = GET_CTX(device)->parameters[parameter];
    mutex_unlock(&etherMutex.handle);
    return ERROR_NONE;
}

error_t apiTransmit(Device* device, const uint8_t* data, size_t length, RadioTxId* id) {
    auto* ctx = GET_CTX(device);
    mutex_lock(&etherMutex.handle);
    const RadioTxId txId = ctx->nextTxId++;
    if (id != nullptr) {
        *id = txId;
    }
    radio_tx_event_emit(device, txId, RADIO_TRANSMISSION_STATE_QUEUED);

    if (ctx->state != RADIO_STATE_ON) {
        radio_tx_event_emit(device, txId, RADIO_TRANSMISSION_STATE_ERROR);
        mutex_unlock(&etherMutex.handle);
        return ERROR_NONE;
    }

    radio_tx_event_emit(device, txId, RADIO_TRANSMISSION_STATE_TRANSMIT_PENDING);
    for (MockRadioCtx* peer = ether; peer != nullptr; peer = peer->next) {
        if (peer != ctx && peer->state == RADIO_STATE_ON && isTunedAlike(ctx, peer)) {
            radio_rx_event_emit(peer->device, data, length, MOCK_RX_RSSI, MOCK_RX_SNR);
        }
    }
    radio_tx_event_emit(device, txId, RADIO_TRANSMISSION_STATE_TRANSMITTED);
    mutex_unlock(&etherMutex.handle);
    return ERROR_NONE;
}

error_t apiGetRssi(Device* device, float* rssi) {
    mutex_lock(&etherMutex.handle);
    const bool on = GET_CTX(device)->state == RADIO_STATE_ON;
    mutex_unlock(&etherMutex.handle);
    if (!on) return ERROR_INVALID_STATE;
    *rssi = MOCK_NOISE_FLOOR;
    return ERROR_NONE;
}

error_t apiIsReceiving(Device* device, bool* receiving) {
    mutex_lock(&etherMutex.handle);
    const bool on = GET_CTX(device)->state == RADIO_STATE_ON;
    mutex_unlock(&etherMutex.handle);
    if (!on) return ERROR_INVALID_STATE;
    // Delivery is instantaneous, so a reception is never in progress
    *receiving = false;
    return ERROR_NONE;
}

error_t startDevice(Device* device) {
    auto* ctx = new (std::nothrow) MockRadioCtx();
    if (ctx == nullptr) return ERROR_OUT_OF_MEMORY;

    ctx->device = device;
    ctx->parameters[RADIO_PARAMETER_FREQUENCY] = 869525000;
    ctx->parameters[RADIO_PARAMETER_BANDWIDTH] = 250000;
    ctx->parameters[RADIO_PARAMETER_SPREADING_FACTOR] = 11;
    ctx->parameters[RADIO_PARAMETER_CODING_RATE] = 5;
    ctx->parameters[RADIO_PARAMETER_SYNC_WORD] = 0x12;
    ctx->parameters[RADIO_PARAMETER_PREAMBLE_LENGTH] = 16;
    ctx->parameters[RADIO_PARAMETER_POWER] = 14;
    ctx->parameters[RADIO_PARAMETER_LBT_RSSI_THRESHOLD] = -90;

    mutex_lock(&etherMutex.handle);
    ctx->next = ether;
    ether = ctx;
    mutex_unlock(&etherMutex.handle);

    device_set_driver_data(device, ctx);
    return ERROR_NONE;
}

error_t stopDevice(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    mutex_lock(&etherMutex.handle);
    for (MockRadioCtx** link = &ether; *link != nullptr; link = &(*link)->next) {
        if (*link == ctx) {
            *link = ctx->next;
            break;
        }
    }
    mutex_unlock(&etherMutex.handle);

    device_set_driver_data(device, nullptr);
    delete ctx;
    return ERROR_NONE;
}

const RadioApi mock_radio_api = {
    .get_radio_state = apiGetRadioState,
    .set_enabled = apiSetEnabled,
    .set_modulation = apiSetModulation,
    .get_modulation = apiGetModulation,
    .can_transmit = apiCanTransmitOrReceive,
    .can_receive = apiCanTransmitOrReceive,
    .set_parameter = apiSetParameter,
    .get_parameter = apiGetParameter,
    .transmit = apiTransmit,
    .get_rssi = apiGetRssi,
    .is_receiving = apiIsReceiving,
};

} // namespace

extern "C" {

Driver mock_radio_driver = {
    .name = "mock_radio",
    .compatible = (const char*[]) { "mock,radio", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .probe = nullptr,
    .api = (const void*)&mock_radio_api,
    .device_type = &RADIO_TYPE,
    .internal = nullptr
};

} // extern "C"
