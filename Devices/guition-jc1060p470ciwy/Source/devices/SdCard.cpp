#include "SdCard.h"

#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>

#include <driver/sdmmc_defs.h>
#include <driver/sdmmc_host.h>
#include <esp_check.h>
#include <esp_ldo_regulator.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

using tt::hal::sdcard::SdCardDevice;

static const auto LOGGER = tt::Logger("JcSdCard");

// ESP32-P4 Slot 0 uses IO MUX (fixed pins, not manually configurable)
// CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42 (defined automatically by hardware)

// TODO: Migrate to "espressif,esp32-sdmmc" driver (needs LDO code porting)
class SdCardDeviceImpl final : public SdCardDevice {

    class NoLock final : public tt::Lock {
        bool lock(TickType_t timeout) const override { return true; }
        void unlock() const override { /* NO-OP */ }
    };

    std::shared_ptr<tt::Lock> lock = std::make_shared<NoLock>();
    sdmmc_card_t* card = nullptr;
    bool mounted = false;
    std::string mountPath;

public:
    SdCardDeviceImpl() : SdCardDevice(MountBehaviour::AtBoot) {}
    ~SdCardDeviceImpl() override {
        if (mounted) {
            unmount();
        }
    }

    std::string getName() const override { return "SD Card"; }
    std::string getDescription() const override { return "SD card via SDMMC host"; }

    bool mount(const std::string& newMountPath) override {
        if (mounted) {
            return true;
        }

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 64 * 1024,
            .disk_status_check_enable = false,
            .use_one_fat = false,
        };

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT; // 20MHz - more stable for initialization
        host.flags = SDMMC_HOST_FLAG_4BIT; // Force 4-bit mode
        // Configure LDO power supply for SD card (critical on ESP32-P4)
        esp_ldo_channel_handle_t ldo_handle = nullptr;
        esp_ldo_channel_config_t ldo_config = {
            .chan_id = 4,  // LDO channel 4 for SD power
            .voltage_mv = 3300,  // 3.3V
            .flags {
                .adjustable = 0,
                .owned_by_hw = 0,
                .bypass = 0
            }
        };

        esp_err_t ldo_ret = esp_ldo_acquire_channel(&ldo_config, &ldo_handle);
        if (ldo_ret != ESP_OK) {
            LOGGER.warn("Failed to acquire LDO for SD power: {} (continuing anyway)", esp_err_to_name(ldo_ret));
        }

        // Slot 0 uses IO MUX - pins are fixed and not specified
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 4;
        slot_config.cd = SDMMC_SLOT_NO_CD;  // No card detect
        slot_config.wp = SDMMC_SLOT_NO_WP;  // No write protect
        slot_config.flags = 0;

        esp_err_t ret = esp_vfs_fat_sdmmc_mount(newMountPath.c_str(), &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            LOGGER.error("Failed to mount SD card: {}", esp_err_to_name(ret));
            card = nullptr;
            return false;
        }

        mountPath = newMountPath;
        mounted = true;
        LOGGER.info("SD card mounted at {}", mountPath);
        return true;
    }

    bool unmount() override {
        if (!mounted) {
            return true;
        }

        esp_err_t ret = esp_vfs_fat_sdcard_unmount(mountPath.c_str(), card);
        if (ret != ESP_OK) {
            LOGGER.error("Failed to unmount SD card: {}", esp_err_to_name(ret));
            return false;
        }
        card = nullptr;
        mounted = false;
        LOGGER.info("SD card unmounted");
        return true;
    }

    std::string getMountPath() const override {
        return mountPath;
    }

    std::shared_ptr<tt::Lock> getLock() const override { return lock; }

    State getState(TickType_t /*timeout*/) const override {
        return mounted ? State::Mounted : State::Unmounted;
    }
};

std::shared_ptr<SdCardDevice> createSdCard() {
    return std::make_shared<SdCardDeviceImpl>();
}
