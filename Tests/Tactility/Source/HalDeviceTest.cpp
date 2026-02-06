#include "doctest.h"
#include <tactility/hal/Device.h>

#include <utility>

using namespace tt;

class TestDevice final : public hal::Device {

private:

    hal::Device::Type type;
    std::string name;
    std::string description;

public:

    TestDevice(hal::Device::Type type, std::string name, std::string description) :
        type(type),
        name(std::move(name)),
        description(std::move(description))
    {}

    TestDevice() : TestDevice(hal::Device::Type::Power, "PowerMock", "PowerMock description") {}

    ~TestDevice() final = default;

    Type getType() const final { return type; }
    std::string getName() const final { return name; }
    std::string getDescription() const final { return description; }
};

class DeviceAutoRegistration {

    std::shared_ptr<hal::Device> device;

public:

    explicit DeviceAutoRegistration(std::shared_ptr<hal::Device> inDevice) : device(std::move(inDevice)) {
        hal::registerDevice(device);
    }

    ~DeviceAutoRegistration() {
        hal::deregisterDevice(device);
    }
};

/** We add 3 tests into 1 to ensure cleanup happens */
TEST_CASE("registering and deregistering a device works") {
    auto device = std::make_shared<TestDevice>();

    // Pre-registration
    CHECK_EQ(hal::findDevice(device->getId()), nullptr);

    // Registration
    hal::registerDevice(device);
    auto found_device = hal::findDevice(device->getId());
    CHECK_NE(found_device, nullptr);
    CHECK_EQ(found_device->getId(), device->getId());

    // Deregistration
    hal::deregisterDevice(device);
    CHECK_EQ(hal::findDevice(device->getId()), nullptr);
    found_device = nullptr; // to decrease use count
    CHECK_EQ(device.use_count(), 1);
}

TEST_CASE("find device by id") {
    auto device = std::make_shared<TestDevice>();
    DeviceAutoRegistration auto_registration(device);

    auto found_device = hal::findDevice(device->getId());
    CHECK_NE(found_device, nullptr);
    CHECK_EQ(found_device->getId(), device->getId());
}

TEST_CASE("find device by name") {
    auto device = std::make_shared<TestDevice>();
    DeviceAutoRegistration auto_registration(device);

    auto found_device = hal::findDevice(device->getName());
    CHECK_NE(found_device, nullptr);
    CHECK_EQ(found_device->getId(), device->getId());
}

TEST_CASE("find device by type") {
    // Headless mode shouldn't have a display, so we want to create one to find only our own display as unique device
    // We first verify the initial assumption that there is no display:
    auto unexpected_display = hal::findFirstDevice<TestDevice>(hal::Device::Type::Display);
    CHECK_EQ(unexpected_display, nullptr);

    auto device = std::make_shared<TestDevice>(hal::Device::Type::Display, "DisplayMock", "");
    DeviceAutoRegistration auto_registration(device);

    auto found_device = hal::findFirstDevice<TestDevice>(hal::Device::Type::Display);
    CHECK_NE(found_device, nullptr);
    CHECK_EQ(found_device->getId(), device->getId());
}
