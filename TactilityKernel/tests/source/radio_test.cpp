#include "doctest.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/radio.h>

#include <cstring>

namespace {

struct FakeRadio {
    bool enabled = false;
    int32_t frequency = 868000000;
    int32_t bandwidth = 125000;
    int32_t power = 14;
    RadioModulation modulation = RADIO_MODULATION_LORA;
};

FakeRadio fake_radio;

error_t fake_get_radio_state(Device*, RadioState* state) {
    *state = fake_radio.enabled ? RADIO_STATE_ON : RADIO_STATE_OFF;
    return ERROR_NONE;
}

error_t fake_set_enabled(Device*, bool enabled) {
    fake_radio.enabled = enabled;
    return ERROR_NONE;
}

error_t fake_set_modulation(Device*, RadioModulation modulation) {
    fake_radio.modulation = modulation;
    return ERROR_NONE;
}

error_t fake_get_modulation(Device*, RadioModulation* modulation) {
    *modulation = fake_radio.modulation;
    return ERROR_NONE;
}

bool fake_can_transmit(Device*, RadioModulation) { return true; }

bool fake_can_receive(Device*, RadioModulation) { return true; }

error_t fake_set_parameter(Device*, RadioParameter parameter, int32_t value) {
    switch (parameter) {
        case RADIO_PARAMETER_FREQUENCY:
            fake_radio.frequency = value;
            return ERROR_NONE;
        case RADIO_PARAMETER_BANDWIDTH:
            fake_radio.bandwidth = value;
            return ERROR_NONE;
        case RADIO_PARAMETER_POWER:
            fake_radio.power = value;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t fake_get_parameter(Device*, RadioParameter parameter, int32_t* value) {
    switch (parameter) {
        case RADIO_PARAMETER_FREQUENCY:
            *value = fake_radio.frequency;
            return ERROR_NONE;
        case RADIO_PARAMETER_BANDWIDTH:
            *value = fake_radio.bandwidth;
            return ERROR_NONE;
        case RADIO_PARAMETER_POWER:
            *value = fake_radio.power;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t fake_transmit(Device*, const uint8_t*, size_t, RadioTxId*) { return ERROR_NONE; }

const RadioApi fake_radio_api = {
    .get_radio_state = fake_get_radio_state,
    .set_enabled = fake_set_enabled,
    .set_modulation = fake_set_modulation,
    .get_modulation = fake_get_modulation,
    .can_transmit = fake_can_transmit,
    .can_receive = fake_can_receive,
    .set_parameter = fake_set_parameter,
    .get_parameter = fake_get_parameter,
    .transmit = fake_transmit,
    .get_rssi = nullptr,
    .is_receiving = nullptr,
};

Driver fake_radio_driver = {
    .name = "fake_radio_driver",
    .compatible = (const char*[]) { "radio_test,fake", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &fake_radio_api,
    .device_type = &RADIO_TYPE,
    .internal = nullptr,
};

/** Constructs, adds and starts a fake radio for the duration of a test case */
struct FakeRadioFixture {
    Device device {
        .name = "fake_radio",
        .config = nullptr,
        .parent = nullptr,
    };

    FakeRadioFixture() {
        fake_radio = {};
        CHECK_EQ(driver_construct_add(&fake_radio_driver), ERROR_NONE);
        CHECK_EQ(device_construct_add(&device, "radio_test,fake"), ERROR_NONE);
        CHECK_EQ(device_start(&device), ERROR_NONE);
    }

    ~FakeRadioFixture() {
        radio_release(&device);
        radio_set_region(&device, RADIO_REGION_UNSET);
        CHECK_EQ(device_stop(&device), ERROR_NONE);
        CHECK_EQ(device_remove(&device), ERROR_NONE);
        CHECK_EQ(device_destruct(&device), ERROR_NONE);
        CHECK_EQ(driver_remove_destruct(&fake_radio_driver), ERROR_NONE);
    }
};

} // namespace

TEST_CASE("radio_acquire grants one owner at a time") {
    FakeRadioFixture fixture;
    Device* device = &fixture.device;

    char owner[RADIO_OWNER_NAME_MAX_LENGTH + 1];
    CHECK_EQ(radio_get_owner(device, owner, sizeof(owner)), ERROR_NOT_FOUND);

    CHECK_EQ(radio_acquire(device, "mesh"), ERROR_NONE);
    CHECK_EQ(radio_acquire(device, "mesh"), ERROR_NONE);
    CHECK_EQ(radio_acquire(device, "sniffer"), ERROR_RESOURCE_BUSY);

    CHECK_EQ(radio_get_owner(device, owner, sizeof(owner)), ERROR_NONE);
    CHECK_EQ(std::strcmp(owner, "mesh"), 0);

    CHECK_EQ(radio_release(device), ERROR_NONE);
    CHECK_EQ(radio_release(device), ERROR_INVALID_STATE);
    CHECK_EQ(radio_acquire(device, "sniffer"), ERROR_NONE);
}

TEST_CASE("radio_get_owner truncates to the buffer size") {
    FakeRadioFixture fixture;
    Device* device = &fixture.device;

    CHECK_EQ(radio_acquire(device, "a-rather-long-owner"), ERROR_NONE);
    char owner[5];
    CHECK_EQ(radio_get_owner(device, owner, sizeof(owner)), ERROR_NONE);
    CHECK_EQ(std::strcmp(owner, "a-ra"), 0);
}

TEST_CASE("radio region gates frequency and power") {
    FakeRadioFixture fixture;
    Device* device = &fixture.device;

    // Without a region nothing is enforced
    CHECK_EQ(radio_get_region(device), RADIO_REGION_UNSET);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 915000000), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 30), ERROR_NONE);

    CHECK_EQ(radio_set_region(device, RADIO_REGION_EU_868), ERROR_NONE);
    CHECK_EQ(radio_get_region(device), RADIO_REGION_EU_868);

    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 915000000), ERROR_OUT_OF_RANGE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 869525000), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 28), ERROR_OUT_OF_RANGE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 27), ERROR_NONE);

    // Unknown parameters pass through to the driver
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_SPREADING_FACTOR, 9), ERROR_NOT_SUPPORTED);
}

TEST_CASE("radio_set_enabled checks the configuration against a single band") {
    FakeRadioFixture fixture;
    Device* device = &fixture.device;

    CHECK_EQ(radio_set_region(device, RADIO_REGION_EU_868), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_BANDWIDTH, 250000), ERROR_NONE);

    // 869.525 MHz with 250 kHz fits the 869.4-869.65 MHz band at 27 dBm
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 869525000), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 27), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, true), ERROR_NONE);
    CHECK(fake_radio.enabled);
    CHECK_EQ(radio_set_enabled(device, false), ERROR_NONE);

    // 868.1 MHz only allows 14 dBm
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 868100000), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, true), ERROR_OUT_OF_RANGE);
    CHECK_FALSE(fake_radio.enabled);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 14), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, true), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, false), ERROR_NONE);

    // The occupied bandwidth must stay inside the band
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 869950000), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, true), ERROR_OUT_OF_RANGE);

    // Disabling is never blocked
    CHECK_EQ(radio_set_enabled(device, false), ERROR_NONE);
}

TEST_CASE("radio regions don't limit ESP-NOW") {
    FakeRadioFixture fixture;
    Device* device = &fixture.device;

    // The most restrictive region
    CHECK_EQ(radio_set_region(device, RADIO_REGION_EU_433), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 20), ERROR_OUT_OF_RANGE);

    // The Wi-Fi stack knows the limits of the country, and it doesn't use the bands of a region
    CHECK_EQ(radio_set_modulation(device, RADIO_MODULATION_ESPNOW), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_POWER, 20), ERROR_NONE);
    CHECK_EQ(radio_set_parameter(device, RADIO_PARAMETER_FREQUENCY, 100000000), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, true), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, false), ERROR_NONE);

    CHECK_EQ(radio_set_modulation(device, RADIO_MODULATION_LORA), ERROR_NONE);
    CHECK_EQ(radio_set_enabled(device, true), ERROR_OUT_OF_RANGE);
}

TEST_CASE("radio_set_region rejects unknown regions") {
    FakeRadioFixture fixture;
    CHECK_EQ(radio_set_region(&fixture.device, RADIO_REGION_COUNT), ERROR_INVALID_ARGUMENT);
}

TEST_CASE("radio optional API returns ERROR_NOT_SUPPORTED when not implemented") {
    FakeRadioFixture fixture;
    float rssi;
    bool receiving;
    uint32_t antenna;
    CHECK_EQ(radio_get_rssi(&fixture.device, &rssi), ERROR_NOT_SUPPORTED);
    CHECK_EQ(radio_is_receiving(&fixture.device, &receiving), ERROR_NOT_SUPPORTED);
    CHECK_EQ(radio_set_antenna(&fixture.device, 0), ERROR_NOT_SUPPORTED);
    CHECK_EQ(radio_get_antenna(&fixture.device, &antenna), ERROR_NOT_SUPPORTED);
}

TEST_CASE("radio region bands") {
    const RadioRegionBand* bands;
    CHECK_EQ(radio_get_region_bands(RADIO_REGION_UNSET, &bands), 0);
    CHECK_EQ(radio_get_region_bands(RADIO_REGION_EU_868, &bands), 3);
    CHECK_EQ(radio_get_region_bands(RADIO_REGION_US_915, &bands), 1);
    CHECK_EQ(bands[0].frequency_min, 902000000);
    CHECK_EQ(std::strcmp(radio_region_to_string(RADIO_REGION_US_915), "US_915"), 0);
}
