#pragma once

#include <tactility/hal/Device.h>

#include <Tactility/TactilityCore.h>
#include <Tactility/Lock.h>

namespace tt::hal::sdcard {

/**
 * Warning: getLock() does not have to be used when calling any of the functions of this class.
 * The lock is only used for file access on the path where the SD card is mounted.
 * This is mainly used when accessing the SD card on a shared SPI bus.
 */
class SdCardDevice : public Device {

public:

    enum class State {
        Mounted,
        Unmounted,
        Error,
        Timeout // Failed to retrieve state due to timeout
    };

    enum class MountBehaviour {
        AtBoot, /** Only mount at boot */
        Anytime /** Mount/dismount any time */
    };

private:

    MountBehaviour mountBehaviour;

public:

    explicit SdCardDevice(MountBehaviour mountBehaviour) : mountBehaviour(mountBehaviour) {}
    ~SdCardDevice() override = default;

    Type getType() const final { return Type::SdCard; };

    /**
     * Mount the device.
     * @param mountPath the path to mount at
     * @return true on successful mount
     */
    virtual bool mount(const std::string& mountPath) = 0;

    /**
     * Unmount the device.
     * @return true on successful unmount
     */
    virtual bool unmount() = 0;

    virtual State getState(TickType_t timeout = kernel::MAX_TICKS) const = 0;

    /** @return empty string when not mounted or the mount path if mounted */
    virtual std::string getMountPath() const = 0;

    /** @return non-null lock, used by code that wants to access files on the mount path of this SD card */
    virtual std::shared_ptr<Lock> getLock() const = 0;

    /** @return the MountBehaviour of this device */
    virtual MountBehaviour getMountBehaviour() const { return mountBehaviour; }

    /** @return true if the SD card was mounted, returns false when it was not or when a timeout happened. */
    bool isMounted(TickType_t timeout = kernel::MAX_TICKS) const { return getState(timeout) == State::Mounted; }
};

/** Return the SdCard device if the path is within the SdCard mounted path (path std::string::starts_with() check), otherwise return nullptr */
std::shared_ptr<SdCardDevice> find(const std::string& path);

/**
 * Attempt to find an SD card that the specified belongs to,
 * and returns its lock if the SD card is mounted. Otherwise it returns nullptr.
 * @param[in] a path on a file system (e.g. file, directory, etc.)
 * @return the lock of a mounted SD card or otherwise null
 */
std::shared_ptr<Lock> findSdCardLock(const std::string& path);

} // namespace tt::hal
